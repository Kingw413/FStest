#include "mine.hpp"
#include "common/logger.hpp"
#include "ndn-wifi-net-device-transport.hpp"
#include "ns3/mobility-model.h"
#include "ns3/ndnSIM/NFD/daemon/fw/algorithm.hpp"
#include "ns3/ndnSIM/model/ndn-net-device-transport.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/apps/ndn-producer.hpp"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/model/ndn-common.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/cs.hpp"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/ptr.h"
#include <cmath>
#include <chrono>

namespace nfd {
namespace fw {
namespace mine {
NFD_LOG_INIT(MINE);
NFD_REGISTER_STRATEGY(MINE);

const double MINE::Rth(200.0);

const time::milliseconds MINE::RETX_SUPPRESSION_INITIAL(10);
const time::milliseconds MINE::RETX_SUPPRESSION_MAX(250);

MINE::MINE(Forwarder& forwarder, const Name& name)
    : Strategy(forwarder),
    m_nodes(ns3::NodeContainer::GetGlobal()),
    m_measurements(getMeasurements()),
    m_retxSuppression(RETX_SUPPRESSION_INITIAL,
                        RetxSuppressionExponential::DEFAULT_MULTIPLIER,
                        RETX_SUPPRESSION_MAX) {
    ParsedInstanceName parsed = parseInstanceName(name);
    if (!parsed.parameters.empty()) {
        NDN_THROW(std::invalid_argument("MINE does not accept parameters"));
    }
    if (parsed.version &&
        *parsed.version != getStrategyName()[-1].toVersion()) {
        NDN_THROW(std::invalid_argument("MINE does not support version " +
                                        to_string(*parsed.version)));
    }
    this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

const Name& MINE::getStrategyName() {
    static Name strategyName("/localhost/nfd/strategy/MINE/%FD%01");
    return strategyName;
}

void MINE::afterReceiveInterest(const FaceEndpoint& ingress,
                               const Interest& interest,
                               const shared_ptr<pit::Entry>& pitEntry)
{
    const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
    const Name prefix = fibEntry.getPrefix();
    const fib::NextHopList& nexthops = fibEntry.getNextHops();
    ns3::Ptr<ns3::Node> localNode = getNode(*this);
    std::set<ns3::Ptr<ns3::Node>> sources = this->getContentSources(interest);
    std::set<Face*> candidates = this->getCandidateForwarders(nexthops, localNode, sources);

    auto it = this->selectFIB(localNode, interest, candidates, fibEntry);
    auto egress = FaceEndpoint(*it, 0);
    NFD_LOG_DEBUG("do Send Interest="<<interest << " from=" << ingress << "to=" << egress);
	this->sendInterest(pitEntry, egress, interest);
    FaceInfo &faceInfo = m_measurements.getOrCreateFaceInfo(fibEntry, interest, egress.face.getId());

    // Refresh measurements since Face is being used for forwarding
    NamespaceInfo &namespaceInfo = m_measurements.getOrCreateNamespaceInfo(fibEntry, interest);
    namespaceInfo.extendFaceInfoLifetime(faceInfo, egress.face.getId());
    ++faceInfo.m_counters.nOutInterests;
}

void
MINE::beforeSatisfyInterest(const shared_ptr<pit::Entry>& pitEntry,
                        const FaceEndpoint& ingress, const Data& data) {

    // NFD_LOG_DEBUG("beforeSatisfyInterest pitEntry=" << pitEntry->getName()
    //             << " in=" << ingress << " data=" << data.getName());

    NamespaceInfo *namespaceInfo = m_measurements.getNamespaceInfo(pitEntry->getName());
    if (namespaceInfo == nullptr) {
        return;
    }

    // Record the RTT between the Interest out to Data in
    FaceInfo *faceInfo = namespaceInfo->getFaceInfo(ingress.face.getId());
    if (faceInfo == nullptr) {
        return;
    }

    auto outRecord = pitEntry->getOutRecord(ingress.face);
    if (outRecord == pitEntry->out_end()) {
    }
    else {
        faceInfo->recordRtt(time::steady_clock::now() - outRecord->getLastRenewed());
        ++faceInfo->m_counters.nSatisfiedInterests;
        double isr = faceInfo->m_counters.nOutInterests == 0.0 ? 0.0 : faceInfo->m_counters.nSatisfiedInterests / faceInfo->m_counters.nOutInterests;
        faceInfo->recordISR(isr);
        // NFD_LOG_DEBUG(pitEntry->getName() << " data from=" << ingress
        //                                   <<" isr="<<faceInfo->getLastISR()<<" sisr="<<faceInfo->getSmoothedISR()
        //                                   << " rtt=" << faceInfo->getLastRtt() << " srtt=" << faceInfo->getSrtt());
    }

    // Extend lifetime for measurements associated with Face
    namespaceInfo->extendFaceInfoLifetime(*faceInfo, ingress.face.getId());
}


void
MINE::afterContentStoreHit(const shared_ptr<pit::Entry>& pitEntry,
                               const FaceEndpoint& ingress, const Data& data)
{
    NFD_LOG_DEBUG("afterContentStoreHit pitEntry=" << pitEntry->getName()
                << " in=" << ingress << " data=" << data.getName());

    this->sendData(pitEntry, data, ingress);
    NFD_LOG_DEBUG("do Send Data="<<data.getName()<<", from="<<ingress);
}

void
MINE::afterReceiveData(const shared_ptr<pit::Entry>& pitEntry,
                           const FaceEndpoint& ingress, const Data& data)
{
  	this->beforeSatisfyInterest(pitEntry, ingress, data);
    // NFD_LOG_DEBUG("afterReceiveData pitEntry=" << pitEntry->getName()
    //             << " in=" << ingress << " data=" << data.getName());
	this->sendDataToAll(pitEntry, ingress, data);
    NFD_LOG_DEBUG("do Send Data="<<data.getName()<<", from="<<ingress);
}

bool 
MINE::isProducer(ns3::Ptr<ns3::Node> node)
{
    if (node->GetNApplications() == 0) {
        return false;
    }
    for (uint32_t i = 0; i < node->GetNApplications(); i++) {
        auto app = node->GetApplication(i);
        ns3::Ptr<ns3::ndn::Producer> producer = app->GetObject<ns3::ndn::Producer>();
        if (producer) {
            return true; 
        }
    }
    return false; 
}

std::set<ns3::Ptr<ns3::Node>>
MINE::getContentSources(const Interest &interest)
{
    std::set<ns3::Ptr<ns3::Node>> sources;
    for (auto &node : m_nodes)
    {
        if (isProducer(node)) {
            sources.emplace(node);
        }
        ns3::Ptr<ns3::ndn::L3Protocol> ndn = node->GetObject<ns3::ndn::L3Protocol>();
        ndn::Name name = interest.getName();
        nfd::cs::Cs &cs = ndn->getForwarder()->getCs();
        for (const auto& entry : cs) {
            if (entry.canSatisfy(interest))
            {
                // std::cout << "Content found in node " <<node->GetId()<< std::endl;
                sources.emplace(node);
                break;
            }
        }
        // // 创建回调函数来处理查找结果
        // auto hitCallback = [&sources, node](nfd::cs::Table::iterator it)
        // {
        //     // 处理找到内容的情况
        //     std::cout << "Content found in CS!" << std::endl;
        //     sources.emplace(node);
        // };
        // auto missCallback = []()
        // {
        //     // 处理未找到内容的情况
        //     std::cout << "Content not found in CS." << std::endl;
        // };
        // cs.find(interest, hitCallback, missCallback);
    }
    return sources;
}

std::set<Face*>
MINE::getCandidateForwarders(const fib::NextHopList &nexthops, ns3::Ptr<ns3::Node> curNode, std::set<ns3::Ptr<ns3::Node>> srcNodes)
{
    std::set<Face*> inRegionSrcs;
    std::set<Face*> candidateForwarders;
    for (auto &srcNode : srcNodes)
    {
        double d_sd = ns3::CalculateDistance(curNode->GetObject<ns3::MobilityModel>()->GetPosition(), srcNode->GetObject<ns3::MobilityModel>()->GetPosition());
        for (auto &nexthop : nexthops)
        {
            // Directly return App Face if it is Producer;
            if (nexthop.getFace().getId() == 256+m_nodes.GetN()) {
                return std::set<Face *>{&nexthop.getFace()};
            }
            uint32_t faceId = nexthop.getFace().getId();
            uint32_t nodeId = (faceId - 257) + (curNode->GetId() + 257 <= faceId);
            ns3::Ptr<ns3::Node> node = m_nodes.Get(nodeId);
            double d_sj = ns3::CalculateDistance(curNode->GetObject<ns3::MobilityModel>()->GetPosition(), node->GetObject<ns3::MobilityModel>()->GetPosition());
            double d_jd = ns3::CalculateDistance(node->GetObject<ns3::MobilityModel>()->GetPosition(), srcNode->GetObject<ns3::MobilityModel>()->GetPosition());
            // 判断是否是Content Source
            if (d_sd < Rth && nodeId == srcNode->GetId()) {
                inRegionSrcs.emplace(&nexthop.getFace());
            }
            if (d_sj < Rth && d_jd <= d_sd)
            {
                candidateForwarders.emplace(&nexthop.getFace());
                // NFD_LOG_DEBUG("Source=" << srcNode->GetId()<<", Candidate="<<nodeId<< ", d_sd=" << d_sd << ", d_sj=" << d_sj << ", d_jd=" << d_jd);
            }
        }
    }
    return inRegionSrcs.size()>0? inRegionSrcs : candidateForwarders;
}

Face*
MINE::selectFIB(ns3::Ptr<ns3::Node> localNode, const Interest &interest, std::set<Face*> candidateForwarders, const fib::Entry &fibEntry)
{   
    std::vector<FaceStats> faceList;
    for (auto& face : candidateForwarders) {
        if (face->getId() == 256 + m_nodes.GetN()) {
            return face;
        }
        uint32_t nodeId = (face->getId() - 257) + (localNode->GetId() + 257 <= face->getId());
        // double distance = this->calculateDistance(localNode, m_nodes[nodeId]);
        double distance = this->caculateDR(localNode, m_nodes[nodeId]);
        FaceInfo *info = m_measurements.getFaceInfo(fibEntry, interest, face->getId());
        if (info == nullptr)
        {
            faceList.push_back({face, distance, 0, 0});
            // NFD_LOG_DEBUG("Face="<<face->getId()<<" has no Info");
        }
        else
        {
            double sisr = info->getSmoothedISR();
            double srtt = 10 - boost::chrono::duration_cast<boost::chrono::duration<double>>(info->getSrtt()).count(); // 正向化处理SRTT指标
            faceList.push_back({face, distance, sisr, srtt});
            // NFD_LOG_DEBUG("Face=" << face->getId() << ", Distance=" << distance << ", SISR=" << sisr << ", SRTT=" << srtt);
        }
    }

    if (faceList.empty()) {
        // NFD_LOG_DEBUG("No Next Hop!");
        return nullptr;
    }
    std::vector<FaceStats> normalizedFaceList  = customNormalize(faceList);
    auto it = this->getOptimalDecision(normalizedFaceList);
    // auto it = std::max_element(normalizedFaceList.begin(), normalizedFaceList.end(), [](const auto& a, const auto& b) { return a.distance+a.sisr+a.srtt < b.distance+b.sisr+b.srtt; });

    // NFD_LOG_DEBUG("Selected Next Hop="<<it.face->getId()<<", Dis="<<it.distance<<", SISR="<<it.sisr<<", SRTT="<<it.srtt);
    return it.face;
}

std::vector<MINE::FaceStats>
MINE::customNormalize(std::vector<FaceStats>& faceList) {
    double letSum=0, lapSum=0, srttSum=0;
    std::vector<FaceStats> normalizedFaceList;
    for (const auto& faceStats: faceList) {
        letSum += pow(faceStats.distance, 2);
        lapSum += pow(faceStats.sisr, 2);
        srttSum += pow(faceStats.srtt, 2);
    }
    for (auto& faceStats : faceList) {
        Face* face = faceStats.face;
        double let =  letSum>0 ? 1.0/3.0 * faceStats.distance / sqrt(letSum) : 0;
        double lap = lapSum>0?  1.0/3.0 * faceStats.sisr / sqrt(lapSum) : 0;
        double srtt = srttSum>0?  1.0/3.0 * faceStats.srtt / sqrt(srttSum) : 0;
        normalizedFaceList.push_back({face, let, lap, srtt});
        // NFD_LOG_DEBUG("Face="<<face->getId()<<", nor_Distance="<<let<<", nor_SISR="<<lap<<", nor_SRTT="<<srtt);
    }
    return normalizedFaceList;
}

MINE::FaceStats
MINE::calculateIdealSolution(std::vector<FaceStats> &faceList) {
    FaceStats idealSolution(
        faceList[0].face, // 使用第一个节点作为默认值
        (std::max_element(faceList.begin(), faceList.end(), [](const auto &a, const auto &b)
                          { return a.distance < b.distance; }))
            ->distance,
        (std::max_element(faceList.begin(), faceList.end(), [](const auto &a, const auto &b)
                          { return a.sisr < b.sisr; }))
            ->sisr,
        (std::max_element(faceList.begin(), faceList.end(), [](const auto &a, const auto &b)
                          { return a.srtt < b.srtt; }))
            ->srtt);
    // NFD_LOG_DEBUG("Ideal: D="<<idealSolution.distance<<", SISR="<<idealSolution.sisr<<", SRTT="<<idealSolution.srtt);
    return idealSolution;
}

MINE::FaceStats
MINE::calculateNegativeIdealSolution(std::vector<FaceStats> &faceList) {
    FaceStats negativeIdealSolution(
        faceList[0].face, // 使用第一个节点作为默认值
        (std::min_element(faceList.begin(), faceList.end(), [](const auto &a, const auto &b)
                          { return a.distance < b.distance; }))
            ->distance,
        (std::min_element(faceList.begin(), faceList.end(), [](const auto &a, const auto &b)
                          { return a.sisr < b.sisr; }))
            ->sisr,
        (std::min_element(faceList.begin(), faceList.end(), [](const auto &a, const auto &b)
                          { return a.srtt < b.srtt; }))
            ->srtt);
    // NFD_LOG_DEBUG("Neg: D=" << negativeIdealSolution.distance << ", SISR=" << negativeIdealSolution.sisr << ", SRTT=" << negativeIdealSolution.srtt);
    return negativeIdealSolution;
}

double
MINE::calculateCloseness(const MINE::FaceStats &entry, const MINE::FaceStats &idealSolution, const MINE::FaceStats &negativeIdealSolution)
{
    double distanceIdealDeviation = entry.distance - idealSolution.distance;
    double relativeVelIdealDeviation = entry.sisr - idealSolution.sisr;
    double LETIdealDeviation = entry.srtt - idealSolution.srtt;
    double closenessToIdeal = sqrt(pow(distanceIdealDeviation, 2) + pow(relativeVelIdealDeviation, 2) + pow(LETIdealDeviation, 2));
    // NFD_LOG_DEBUG("disToIdeal="<<distanceIdealDeviation<<", velToIdeal="<<relativeVelIdealDeviation<<", letIdeal="<<LETIdealDeviation<<" clossToIdeal="<<closenessToIdeal);

    double distanceNegDeviation = entry.distance - negativeIdealSolution.distance;
    double relativeVelNegDeviation = entry.sisr - negativeIdealSolution.sisr;
    double LETNegDeviation = entry.srtt - negativeIdealSolution.srtt;
    double closenessToNeg = sqrt(pow(distanceNegDeviation, 2) + pow(relativeVelNegDeviation, 2) + pow(LETNegDeviation, 2));
    // NFD_LOG_DEBUG("disToNeg="<<distanceNegDeviation<<", velToIdeal="<<relativeVelNegDeviation<<", letIdeal="<<LETNegDeviation<<" clossToIdeal="<<closenessToNeg);

    double closeness = closenessToNeg / (closenessToIdeal + closenessToNeg);
    // NFD_LOG_DEBUG("node="<<entry.node->GetId()<<", closeness="<<closeness);

    return closeness;
}

MINE::FaceStats&
MINE::getOptimalDecision(std::vector<MINE::FaceStats> &faceList)
{
    std::vector<double> closenessValues;
    FaceStats idealSolution = this->calculateIdealSolution(faceList);
    FaceStats negIdealSolution = this->calculateNegativeIdealSolution(faceList);
    for (const auto &entry : faceList)
    {
        double closeness = this->calculateCloseness(entry, idealSolution, negIdealSolution);
        closenessValues.push_back(closeness);
    }
    size_t optIndex = std::distance(closenessValues.begin(), std::max_element(closenessValues.begin(), closenessValues.end()));
    FaceStats& optimalDecision = faceList[optIndex];
        // NFD_LOG_DEBUG("Optimal Decision = " << optimalDecision.face->getId());
    return optimalDecision;
}


double
MINE::calculateDistance(ns3::Ptr<ns3::Node> node1, ns3::Ptr<ns3::Node> node2) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = node1->GetObject<ns3::MobilityModel>();
    ns3::Ptr<ns3::MobilityModel> mobility2 = node2->GetObject<ns3::MobilityModel>();
    double d = mobility1->GetDistanceFrom(mobility2) + 0.0001;
	return d;
}

double
MINE::caculateDR(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode)
{
    double eculid = this->calculateDistance(sendNode, receiveNode);
    ns3::Ptr<ns3::MobilityModel> mobility = sendNode->GetObject<ns3::MobilityModel>();
    ns3::Vector3D nodePos = mobility->GetPosition();
    ns3::Ptr<ns3::MobilityModel> remoteMob = receiveNode->GetObject<ns3::MobilityModel>();
    ns3::Vector3D remotePos = remoteMob->GetPosition();
    // ns3::Vector3D direction = remoteMob->GetVelocity();
    ns3::Vector3D direction = {1.0, 0.0, 0.0};
    double angle = std::atan2(direction.x, direction.y) - std::atan2(remotePos.x - nodePos.x, remotePos.y - nodePos.y);
    double dr = abs(eculid * cos(angle));
    return dr;
}

double
MINE::calculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = revNode->GetObject<ns3::MobilityModel>();
    if (mobility1->GetDistanceFrom(mobility2) >= Rth) {return 0;}
    double m = mobility1->GetPosition().x - mobility2->GetPosition().x;
    double n = mobility1->GetPosition().y - mobility2->GetPosition().y;
    double p = mobility1->GetVelocity().x - mobility2->GetVelocity().x;
    double q = mobility1->GetVelocity().y - mobility2->GetVelocity().y;
    if (p==0 && q==0) {return 1e6;} //相对速度为0时，用1e6表示无限大
    double let = (-(m*p+n*q)+sqrt((pow(p,2)+pow(q,2))*pow(Rth,2) - pow(n*p-m*q, 2)) ) / (pow(p,2)+pow(q,2));
    return let;
}

ns3::Ptr<ns3::Node>
MINE::getNode(MINE& local_strategy) {
    ns3::Ptr<ns3::Node> localNode;
    for (const auto &node : m_nodes)
    {
        ns3::Ptr<ns3::ndn::L3Protocol> ndn = node->GetObject<ns3::ndn::L3Protocol>();
        ndn::Name prefix("/ustc");
        //此处有个坑：注册nfd时先是采用BestRouteStrategy2策略，其需要先发送set信息，因此需保证此时已经设置完毕策略
        nfd::fw::Strategy &strategy = ndn->getForwarder()->getStrategyChoice().findEffectiveStrategy(prefix);
        nfd::fw::mine::MINE &MINE_strategy = dynamic_cast<nfd::fw::mine::MINE &>(strategy);
        if (this == &MINE_strategy)
        {
            return node; 
        }
    }
}

}  // namespace mine
}  // namespace fw
}  // namespace nfd