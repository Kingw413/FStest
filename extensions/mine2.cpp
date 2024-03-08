#include "mine2.hpp"
#include "mine2-measurements.hpp"
#include "common/logger.hpp"
#include "ndn-wifi-net-device-transport.hpp"
#include "ns3/mobility-model.h"
#include "ns3/ndnSIM/NFD/daemon/fw/algorithm.hpp"
#include "ns3/ndnSIM/model/ndn-net-device-transport.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/model/ndn-common.hpp"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/ptr.h"
#include <cmath>
#include <ndn-cxx/lp/empty-value.hpp>
#include <ndn-cxx/lp/prefix-announcement-header.hpp>
#include <ndn-cxx/lp/tags.hpp>
#include <chrono>
#include "ns3/ndnSIM/NFD/daemon/table/fib-entry.hpp"

namespace nfd {
namespace fw {
namespace mine2 {
NFD_LOG_INIT(MINE2);
NFD_REGISTER_STRATEGY(MINE2);

const double MINE2::Rth(200.0);
const double MINE2::Mu(0.33);
const double MINE2::Phi(0.33);
const double MINE2::Omega(0.33); 
const double MINE2::Alpha(0.5); 
const double MINE2::Beta(0.5); 
const double MINE2::LETMAX(50);

const time::milliseconds MINE2::RETX_SUPPRESSION_INITIAL(10);
const time::milliseconds MINE2::RETX_SUPPRESSION_MAX(250);

MINE2::MINE2(Forwarder& forwarder, const Name& name)
    : Strategy(forwarder),
    m_nodes(ns3::NodeContainer::GetGlobal()),
    m_measurements(getMeasurements()),
    m_retxSuppression(RETX_SUPPRESSION_INITIAL,
                        RetxSuppressionExponential::DEFAULT_MULTIPLIER,
                        RETX_SUPPRESSION_MAX) {
    ParsedInstanceName parsed = parseInstanceName(name);
    if (!parsed.parameters.empty()) {
        NDN_THROW(std::invalid_argument("MINE2 does not accept parameters"));
    }
    if (parsed.version &&
        *parsed.version != getStrategyName()[-1].toVersion()) {
        NDN_THROW(std::invalid_argument("MINE2 does not support version " +
                                        to_string(*parsed.version)));
    }
    this->setInstanceName(makeInstanceName(name, getStrategyName()));
    ns3::Simulator::Schedule(ns3::Seconds(1.0), &MINE2::updateISR, this);
}

const Name& MINE2::getStrategyName() {
    static Name strategyName("/localhost/nfd/strategy/MINE2/%FD%01");
    return strategyName;
}

void MINE2::afterReceiveInterest(const FaceEndpoint& ingress,
                               const Interest& interest,
                               const shared_ptr<pit::Entry>& pitEntry)
{   
    const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
    const Name prefix = fibEntry.getPrefix();
    const fib::NextHopList& nexthops = fibEntry.getNextHops();
    ns3::Ptr<ns3::Node> localNode = getNode(*this);
    //因为创建无线face时在FIB中添加了“/”的路由，因此匹配到“/”等价于FIB中无匹配项
    if (prefix == "/") {
        NFD_LOG_DEBUG("Interest="<<interest << " from=" << ingress<<" no match in FIB, do Content Discovery!");
        contentDiscovery(localNode, ingress, nexthops, interest, pitEntry);
        return;
    }
    // 更新FIB
    for (const auto& providerNode : m_CPT) {
        // NFD_LOG_DEBUG("update CPT P="<<providerNode->GetId());
        this->updateFIB(prefix, localNode, providerNode, nexthops, fibEntry);
    }
    //此处有一个小bug，updateFIB中的AddRoute操作需要时间，因此此时读取的fibEntry并非更新后的，更新后的FIB表项下一次才会起作用
    auto it = this->selectFIB(localNode, interest, ingress, nexthops, fibEntry);
    if (it==nullptr) {
        NFD_LOG_DEBUG("No Next Hop!");
        return;
    }
	auto egress = FaceEndpoint(*it, 0);
    NFD_LOG_DEBUG("do Send Interest="<<interest << " from=" << ingress << "to=" << egress);
	this->sendInterest(pitEntry, egress, interest);

    FaceInfo &faceInfo = m_measurements.getOrCreateFaceInfo(fibEntry, interest, egress.face.getId());

    // Refresh measurements since Face is being used for forwarding
    NamespaceInfo &namespaceInfo = m_measurements.getOrCreateNamespaceInfo(fibEntry, interest);
    namespaceInfo.extendFaceInfoLifetime(faceInfo, egress.face.getId());
    ++faceInfo.m_counters.nOutInterests;
    // NFD_LOG_DEBUG("nOutInterests="<<faceInfo.m_counters.nOutInterests);
}

void
MINE2::afterReceiveLoopedInterest(const FaceEndpoint& ingress, const Interest& interest,
                                     pit::Entry& pitEntry)
{
//   NFD_LOG_DEBUG("afterReceiveLoopedInterest pitEntry=" << pitEntry.getName()
//                 << " in=" << ingress);
}

void
MINE2::afterContentStoreHit(const shared_ptr<pit::Entry>& pitEntry,
                               const FaceEndpoint& ingress, const Data& data) {
    NFD_LOG_DEBUG("do Send Data="<<data.getName()<<", from="<<ingress);
    this->sendData(pitEntry, data, ingress);
}

void
MINE2::afterReceiveData(const shared_ptr<pit::Entry>& pitEntry,
                           const FaceEndpoint& ingress, const Data& data)
{
  	this->beforeSatisfyInterest(pitEntry, ingress, data);
	this->sendDataToAll(pitEntry, ingress, data);
    NFD_LOG_DEBUG("do Send Data="<<data.getName()<<", from="<<ingress);
}

void
MINE2::beforeSatisfyInterest(const shared_ptr<pit::Entry>& pitEntry,
                        const FaceEndpoint& ingress, const Data& data) {

    const ndn::Name prefix = pitEntry->getName().getPrefix(1);
    bool isDiscovery = pitEntry->getInterest().getTag<lp::NonDiscoveryTag>() != nullptr;
    // 收到相应的Discovery的Data包后触发操作
    if (isDiscovery && ingress.face.getId()!=254)
    {
        afterReceiveDiscoveryData(pitEntry, ingress, data);
        return; // 是否需要测量Discovery包的RTT？
    }

    // 对于正常的Interest，记录端口的指标信息
    NamespaceInfo *namespaceInfo = m_measurements.getNamespaceInfo(pitEntry->getName());
    if (namespaceInfo == nullptr)
    {
        // NFD_LOG_DEBUG(pitEntry->getName() << " data from=" << ingress << " no-measurements");
        return;
    }

    // Record the RTT between the Interest out to Data in
    FaceInfo *faceInfo = namespaceInfo->getFaceInfo(ingress.face.getId());
    if (faceInfo == nullptr)
    {
        // NFD_LOG_DEBUG(pitEntry->getName() << " data from=" << ingress << " no-face-info");
        return;
    }

    auto outRecord = pitEntry->getOutRecord(ingress.face);
    if (outRecord == pitEntry->out_end())
    {
        // NFD_LOG_DEBUG(pitEntry->getName() << " data from=" << ingress << " no-out-record");
    }
    else
    {
        faceInfo->recordRtt(time::steady_clock::now() - outRecord->getLastRenewed());
        ++faceInfo->m_counters.nSatisfiedInterests;
        // NFD_LOG_DEBUG(pitEntry->getName() << " data from=" << ingress
        //                                   << " rtt=" << faceInfo->getLastRtt() << " srtt=" << faceInfo->getSrtt()<<" isr="<<faceInfo->getLastISR()<<" sisr="<<faceInfo->getSmoothedISR());
    }

    namespaceInfo->extendFaceInfoLifetime(*faceInfo, ingress.face.getId());
}

void
MINE2::afterReceiveDiscoveryData(const shared_ptr<pit::Entry>& pitEntry,
                        const FaceEndpoint& ingress, const Data& data) {
    // 若是Content Discovery包，则需要延长pitEntry的生命周期，以等待不同上游的Data包返回
    // this->setExpiryTimer(pitEntry, 5000_ms);

    ns3::Ptr<ns3::Node> localNode = getNode(*this);
    
    // 当Interest到达Producer后，将Provider ID添加到Data包的`CongestionMarkTag`中。
    if (ingress.face.getId() == 256 + m_nodes.GetN())
    {
        // NFD_LOG_DEBUG("Set Provider ID Tag=" << localNode->GetId());
        data.setTag(make_shared<lp::CongestionMarkTag>(localNode->GetId()));
        return;
    }

    // v1.2版本：添加初始Discovery包的返回路径，以在unicast path building阶段找不到路径时提供备选
    ns3::Ptr<ns3::ndn::L3Protocol> ndn = localNode->GetObject<ns3::ndn::L3Protocol>();
    shared_ptr<Face> face = ndn->getFaceById(ingress.face.getId());
    ndn::Name prefix = pitEntry->getName().getPrefix(1);
    ns3::ndn::FibHelper::AddRoute(localNode, prefix, face, 1e6);
    // NFD_LOG_DEBUG("Add Route: Node="<<localNode->GetId()<<", Prefix="<<prefix<<", Face="<<ingress.face.getId()<<", Score="<<1e6);

    // 当Data返回到请求的Consumer端时，结束泛洪检索，并触发路径建立过程。
    uint64_t requesterId = pitEntry->getInterest().getTag<lp::CongestionMarkTag>()->get();
    if (requesterId == localNode->GetId())
    {
        // NFD_LOG_DEBUG("Content Discovery Finished!"<<" data="<<data.getName()<< " In="<<ingress);
        uint64_t providerNodeId = data.getTag<lp::CongestionMarkTag>()->get();
        ns3::Ptr<ns3::Node> providerNode = m_nodes[providerNodeId];

        // 构建CPT记录Provider。为了简单，直接将Provider ID添加到所有节点的CPT中（实际上只需要在相应的Consumer处添加即可）
        this->createCPT(providerNode);
        // 触发路径建立
        const ndn::Name prefix = pitEntry->getName().getPrefix(1);
        this->unicastPathBuilding(prefix, localNode, providerNode);
    }
}

void
MINE2::contentDiscovery(ns3::Ptr<ns3::Node> localNode, const FaceEndpoint& ingress, const fib::NextHopList& nexthops, const Interest& interest, const shared_ptr<pit::Entry> &pitEntry) {
    if (ingress.face.getId()==256+m_nodes.GetN()) {
        // 使用NonDiscoveryTag标识是否是用于Content Discovery的Interest包
        interest.setTag(make_shared<lp::NonDiscoveryTag>(lp::EmptyValue{}));
        // 使用Interest包的CongestionMarkTag标识Requseter ID
        interest.setTag(make_shared<lp::CongestionMarkTag>(localNode->GetId()));
        // NFD_LOG_DEBUG("Set Requester ID="<<interest.getTag<lp::CongestionMarkTag>()->get());
    }

    for (const auto& nexthop : nexthops) {
        if (!isNextHopEligible(ingress, nexthop, localNode))
            continue;
        auto egress = FaceEndpoint(nexthop.getFace(), 0);
        this->sendInterest(pitEntry, egress, interest);
        // NFD_LOG_DEBUG("do Discovery Interest="<<interest << " from=" << ingress << "to=" << egress);
    }
}

void
MINE2::createCPT(ns3::Ptr<ns3::Node> providerNode) {
    for (const auto& node : m_nodes)
    {
        ns3::Ptr<ns3::ndn::L3Protocol> ndn = node->GetObject<ns3::ndn::L3Protocol>();
        ndn::Name prefix("/");
        nfd::fw::Strategy &strategy = ndn->getForwarder()->getStrategyChoice().findEffectiveStrategy(prefix);
        nfd::fw::mine2::MINE2 &mine2_strategy = dynamic_cast<nfd::fw::mine2::MINE2 &>(strategy);
        mine2_strategy.m_CPT.push_back(providerNode);
    }
}

void
MINE2::unicastPathBuilding(const ndn::Name prefix, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> providerNode) {
    // 初始时，添加路径起点，即Consumer
    if (m_path.empty()) {
        m_path.emplace(providerNode, std::set<ns3::Ptr<ns3::Node>>{srcNode});
    }
    
    // 循环执行直到Producer在当前relay的范围内
    while ( !isInRegion(srcNode, providerNode) ) {
        std::vector<MINE2::weightTableEntry> weightTable;
        for (const auto& node : m_nodes) {
            if ( isInPath(node, providerNode) || !isIntermediateNode(node, srcNode, providerNode)) { continue; }
            double dis = this->calculateDistance(node, srcNode, providerNode);
            double dir = this->calculateDirection(node, srcNode, providerNode);
            double den = this->calculateDensity(node);
            double score = Mu*dis + Phi*dir + Omega*den;
            weightTableEntry entry = {node, dis, dir, den, score};
            weightTable.push_back(entry);
            // NFD_LOG_DEBUG("neiNode="<<node->GetId()<<", dis="<<dis<<", dir="<<dir<<", den="<<den<<", score="<<score);
        }
        if (weightTable.size()==0) {
            NFD_LOG_DEBUG("There is no Path!");
            return;
        }
        ns3::Ptr<ns3::Node> selectNode = std::max_element(weightTable.begin(), weightTable.end(), [](const auto& a, const auto& b) { return a.Score<b.Score;})->node;
        double score = std::max_element(weightTable.begin(), weightTable.end(), [](const auto& a, const auto& b) { return a.Score<b.Score;})->Score;
        ns3::Ptr<ns3::ndn::L3Protocol> ndn = selectNode->GetObject<ns3::ndn::L3Protocol>();
        uint32_t faceId = (selectNode->GetId()+256) + (selectNode->GetId() < srcNode->GetId());
        shared_ptr<Face> face = ndn->getFaceById(faceId);
        ns3::ndn::FibHelper::AddRoute(srcNode, prefix, face, score*1e6);
        // NFD_LOG_DEBUG("Add Route: Node="<<srcNode->GetId()<<", Prefix="<<prefix<<", Face="<<faceId<<", Score="<<score);
        srcNode = selectNode;
        // 添加路径
        auto pair = m_path.find(providerNode);
        auto it = pair->second.insert(selectNode);
        NS_ASSERT(it.second);
    }
    ns3::Ptr<ns3::ndn::L3Protocol> ndn = srcNode->GetObject<ns3::ndn::L3Protocol>();
    uint32_t faceId = (providerNode->GetId()+256) + (providerNode->GetId() < srcNode->GetId());
    shared_ptr<Face> face = ndn->getFaceById(faceId);
    ns3::ndn::FibHelper::AddRoute(srcNode, prefix, face, 1e6);
    // NFD_LOG_DEBUG("Add Route: Node="<<srcNode->GetId()<<", Prefix="<<prefix<<", Face="<<faceId<<", Score="<<1.0);
}

void
MINE2::updateFIB(const ndn::Name prefix, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> providerNode,  const fib::NextHopList& nexthops, const fib::Entry& fibEntry) {
    NFD_LOG_DEBUG("updateFIB");
    if (nexthops.begin()->getFace().getId() == 256+m_nodes.GetN()) {return;}
    double min_score = std::min_element(nexthops.begin(), nexthops.end(), [](const auto& a, const auto& b) {return a.getCost() < b.getCost();})->getCost() / 1e6;
    for (const auto& oth_node : m_nodes) {
        if ( !isIntermediateNode(oth_node, srcNode, providerNode)) { continue; }
        double dis = this->calculateDistance(oth_node, srcNode, providerNode);
        double dir = this->calculateDirection(oth_node, srcNode, providerNode);
        double den = this->calculateDensity(oth_node);
        double score = Mu*dis + Phi*dir + Omega*den;
        if (score > min_score) {
            ns3::Ptr<ns3::ndn::L3Protocol> ndn = srcNode->GetObject<ns3::ndn::L3Protocol>();
            uint32_t faceId = (oth_node->GetId()+256) + (oth_node->GetId() < srcNode->GetId());
            shared_ptr<Face> face = ndn->getFaceById(faceId);
            ns3::ndn::FibHelper::AddRoute(srcNode, prefix, face, score*1e6);
            // NFD_LOG_DEBUG("updateFIB: Node="<<srcNode->GetId()<<", Prefix="<<prefix<<", Face="<<faceId<<", New_Score="<<score<<", Min_Score="<<min_score);
        }
    }
}

Face*
MINE2::selectFIB(ns3::Ptr<ns3::Node> localNode, const Interest& interest, const FaceEndpoint& ingress, const fib::NextHopList& nexthops, const fib::Entry& fibEntry) {
    auto selectedHop = *nexthops.begin();
    if (selectedHop.getFace().getId() == 256+m_nodes.GetN()) {return &selectedHop.getFace();}
    std::vector<FaceStats> faceList;
    for(const auto& nexthop : nexthops) {
        if (ingress.face.getId()==nexthop.getFace().getId() ) {continue;};
        uint32_t faceId = nexthop.getFace().getId();
        uint32_t othNodeId = (faceId - 257) + (localNode->GetId() + 257 <= faceId);
        ns3::Ptr<ns3::Node>othNode = m_nodes[othNodeId];
        double let = this->calculateLET(localNode, othNode);
        double link_prob = this->calculateLAP(let, 2.0);
        if (let==0) {continue;} //补丁操作，防止选择LET=0的链路
        // bool isinregion = this->isInRegion(localNode, othNode);
        // if(isinregion) { NFD_LOG_DEBUG("TEST2");
        // if(!isinregion) {continue;}
        FaceInfo* info = m_measurements.getFaceInfo(fibEntry, interest, nexthop.getFace().getId());
        if (info == nullptr) {
            faceList.push_back({&nexthop.getFace(),let, link_prob, 0, 0});
            // NFD_LOG_DEBUG("Face="<<nexthop.getFace().getId()<<", LET="<<let<<", LAP="<<link_prob<<", SRTT="<<-1<<", ISR="<<-1);
        }
        else {
            double srtt = 10 - boost::chrono::duration_cast<boost::chrono::duration<double>>(info->getSrtt()).count(); // 正向化处理SRTT指标
            double isr = info->getSmoothedISR();
            faceList.push_back({&nexthop.getFace(),let, link_prob, srtt, isr});
            // NFD_LOG_DEBUG("Face="<<nexthop.getFace().getId()<<", LET="<<let<<", LAP="<<link_prob<<", SRTT="<<srtt<<", ISR="<<isr);
        } 
    }
    if(faceList.empty()) {return nullptr;}
    std::vector<FaceStats> normalizedFaceList  = customNormalize(faceList);
    // for (const auto& it: normalizedFaceList) {
    //         NFD_LOG_DEBUG("Selected Next Hop="<<it.face->getId()<<", LET="<<it.let<<", LAP="<<it.lap<<", SRTT="<<it.srtt<<", ISR="<<it.sisr);
    // }
    auto it = std::max_element(normalizedFaceList.begin(), normalizedFaceList.end(), [](const auto& a, const auto& b) { return a.let+a.lap+a.srtt+a.sisr < b.let+b.lap+b.srtt+b.sisr; });
    // NFD_LOG_DEBUG("Selected Next Hop="<<it->face->getId()<<", LET="<<it->let<<", LAP="<<it->lap<<", SRTT="<<it->srtt<<", ISR="<<it->sisr);
    return it != normalizedFaceList.end() ? it->face : nullptr;
}

bool
MINE2::isIntermediateNode(ns3::Ptr<ns3::Node> node, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> desNode) {
    double d_sd = ns3::CalculateDistance(srcNode->GetObject<ns3::MobilityModel>()->GetPosition(), desNode->GetObject<ns3::MobilityModel>()->GetPosition());
    double R2 = 1.5*d_sd - Rth;
    double d_sj = ns3::CalculateDistance(srcNode->GetObject<ns3::MobilityModel>()->GetPosition(), node->GetObject<ns3::MobilityModel>()->GetPosition());
    double d_jd = ns3::CalculateDistance(desNode->GetObject<ns3::MobilityModel>()->GetPosition(), node->GetObject<ns3::MobilityModel>()->GetPosition());
    if (d_sj <= Rth && d_jd <= R2) {
        return true;
    }
    return false;
}

bool
MINE2::isInPath(ns3::Ptr<ns3::Node> node, ns3::Ptr<ns3::Node> desNode) {
    auto path = m_path.find(desNode);
    auto it = std::find(path->second.begin(), path->second.end(), node);
    return it!=path->second.end();
}

bool
MINE2::isInRegion(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> recvNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = recvNode->GetObject<ns3::MobilityModel>();
    double distance = mobility1->GetDistanceFrom(mobility2);
    return distance <= Rth;
}

bool
MINE2::isNextHopEligible(const FaceEndpoint& ingress, const fib::NextHop& nexthop, ns3::Ptr<ns3::Node> node) {
    if (nexthop.getFace().getId() == ingress.face.getId()) 
        return false;
    
    if (nexthop.getFace().getId()==m_nodes.GetN()+256) 
        return true;
    
    uint32_t othNodeId = (nexthop.getFace().getId() - 257) + (node->GetId() + 257 <= nexthop.getFace().getId());
    ns3::Ptr<ns3::Node>othNode = m_nodes[othNodeId];
    return isInRegion(node, othNode);
}

void
MINE2::updateISR() {
    ns3::Simulator::Schedule(ns3::Seconds(1.0), &MINE2::updateISR, this);
    NamespaceInfo *namespaceInfo = m_measurements.getNamespaceInfo("/ustc");
    if (!namespaceInfo) {
        // NFD_LOG_DEBUG("no NamespaceInfo");
        return;  
    }
    auto& fiMap = namespaceInfo->m_fiMap;
    if (fiMap.empty()) {
        // NFD_LOG_DEBUG("fiMap is empty");
        return;  
    }
    for (auto& pair : fiMap) {
        FaceInfo& info = pair.second;
        double isr = info.m_counters.nOutInterests==0.0 ? 0.0 : info.m_counters.nSatisfiedInterests / info.m_counters.nOutInterests;
         info.recordISR(isr);
        // NFD_LOG_DEBUG("ISR="<<isr<<" SISR="<<info.getSmoothedISR());
        info.m_counters.nSatisfiedInterests.set(0);
        info.m_counters.nOutInterests.set(0);
    }
}

std::vector<MINE2::FaceStats>
MINE2::customNormalize(std::vector<FaceStats>& faceList) {
    double letSum=0, lapSum=0, srttSum=0, isrSum=0;
    std::vector<FaceStats> normalizedFaceList;
    for (const auto& faceStats: faceList) {
        letSum += pow(faceStats.let, 2);
        lapSum += pow(faceStats.lap, 2);
        srttSum += pow(faceStats.srtt, 2);
        isrSum += pow(faceStats.sisr, 2);
    }
    for (auto& faceStats : faceList) {
        Face* face = faceStats.face;
        double let =  letSum>0 ? 1.0/3.0 * faceStats.let / sqrt(letSum) : 0;
        double lap = lapSum>0?  1.0/3.0 * faceStats.lap / sqrt(lapSum) : 0;
        double srtt = srttSum>0?  1.0/3.0 * faceStats.srtt / sqrt(srttSum) : 0;
        double sisr = isrSum>0?  1.0/3.0 * faceStats.sisr / sqrt(isrSum) : 0;
        normalizedFaceList.push_back({face, let, lap, srtt, sisr});
        // NFD_LOG_DEBUG("Face="<<face->getId()<<", nor_LET="<<let<<", nor_LAP="<<lap<<", nor_SRTT="<<srtt<<", nor_ISR="<<sisr);
    }
    return normalizedFaceList;
}

double
MINE2::calculateDistance(ns3::Ptr<ns3::Node> localNode, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> desNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = localNode->GetObject<ns3::MobilityModel>();
    ns3::Ptr<ns3::MobilityModel> mobility2 = srcNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility3 = desNode->GetObject<ns3::MobilityModel>();
	double d_jd =  mobility1->GetDistanceFrom(mobility3)+0.0001;
	double d_sd =  mobility2->GetDistanceFrom(mobility3);
	return std::max(log(d_sd / d_jd +0.0001), 0.1);
}

double
MINE2::calculateDirection(ns3::Ptr<ns3::Node> node, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> desNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = node->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = srcNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility3 = desNode->GetObject<ns3::MobilityModel>();
    ns3::Vector a = mobility1->GetVelocity(); 
    ns3::Vector b = mobility2->GetPosition() - mobility3->GetPosition();
    // NFD_LOG_DEBUG("a.x="<<a.x<<", a.y="<<a.y<<", b.x="<<b.x<<", b.y="<<b.y<<"|a|="<<a.GetLength()<<"|b|="<<b.GetLength());
    double dir = (a.x * b.x + a.y * b.y) / ( (a.GetLength()+0.0001) *(b.GetLength()+0.0001)); // 防止分母为0
    return dir;
}

double
MINE2::calculateDensity(ns3::Ptr<ns3::Node> node){
    int num_avg = 0;
    double num_con=0.0; // 网络平均连接度
    for (const auto& node1 :  m_nodes) {
        int num_neighbor = 0;
        for (const auto& node2 : m_nodes) {
            double d = ns3::CalculateDistance(node1->GetObject<ns3::MobilityModel>()->GetPosition(), node2->GetObject<ns3::MobilityModel>()->GetPosition());
            num_neighbor += (d<Rth);
        }
        num_neighbor -= 1; // 去掉自身
        if (node1 == node) { num_avg = num_neighbor;}
        num_con += num_neighbor;
    }
    num_con = num_con / m_nodes.GetN();
    double td = num_avg / num_con;
    return std::min(td, 1.0);
}

double
MINE2::calculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode) {
    if (!isInRegion(sendNode, revNode)) { return 0;}
    ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = revNode->GetObject<ns3::MobilityModel>();
    double m = mobility1->GetPosition().x - mobility2->GetPosition().x;
    double n = mobility1->GetPosition().y - mobility2->GetPosition().y;
    double p = mobility1->GetVelocity().x - mobility2->GetVelocity().x;
    double q = mobility1->GetVelocity().y - mobility2->GetVelocity().y;
    // NFD_LOG_DEBUG("srcNode="<<sendNode->GetId()<<", X="<<mobility1->GetPosition().x<<", Y="<<mobility1->GetPosition().y<<", Vx="<<mobility1->GetVelocity().x<<", Vy="<<mobility1->GetVelocity().y);
    // NFD_LOG_DEBUG("revNode="<<revNode->GetId()<<", X="<<mobility2->GetPosition().x<<", Y="<<mobility2->GetPosition().y<<", Vx="<<mobility2->GetVelocity().x<<", Vy="<<mobility2->GetVelocity().y);
    if (p==0 && q==0) {return LETMAX;} //相对速度为0时，用1e6表示无限大
    double let = (-(m*p+n*q)+sqrt((pow(p,2)+pow(q,2))*pow(Rth,2) - pow(n*p-m*q, 2)) ) / (pow(p,2)+pow(q,2));
    // NFD_LOG_DEBUG("m="<<m<<", n="<<n<<", p="<<p<<", q="<<q<<", LET="<<let);
    return let>=LETMAX? LETMAX : let;
}

double
MINE2::calculateLAP(double t, double delta_t) {
    if (t==0) {return 0;}
    double lambda = 10;
    double L = (1.0-exp(-2*lambda*t)) * (1.0/(2*lambda*t)) + 0.5*lambda*t*exp(-2*lambda*t);
    double prob = delta_t <= t ?  (1.0-(1.0-L)/t * delta_t) : L/(log(delta_t-t+1)+1);
    return prob;
}

ns3::Ptr<ns3::Node>
MINE2::getNode(MINE2& local_strategy) {
    ns3::Ptr<ns3::Node> localNode;
    for (const auto &node : m_nodes)
    {
        ns3::Ptr<ns3::ndn::L3Protocol> ndn = node->GetObject<ns3::ndn::L3Protocol>();
        ndn::Name prefix("/ustc");
        //此处有个坑：注册nfd时先是采用BestRouteStrategy2策略，其需要先发送set信息，因此需保证此时已经设置完毕策略
        nfd::fw::Strategy &strategy = ndn->getForwarder()->getStrategyChoice().findEffectiveStrategy(prefix);
        nfd::fw::mine2::MINE2 &MINE2_strategy = dynamic_cast<nfd::fw::mine2::MINE2 &>(strategy);
        if (this == &MINE2_strategy)
        {
            return node; 
        }
    }
}


}  // namespace MINE2
}  // namespace fw
}  // namespace nfd