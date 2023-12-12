#include "mupf.hpp"
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

namespace nfd {
namespace fw {

NFD_LOG_INIT(MUPF);
NFD_REGISTER_STRATEGY(MUPF);

const double MUPF::Rth(100.0);
const double MUPF::Mu(0.33);
const double MUPF::Phi(0.33);
const double MUPF::Omega(0.33); 
const double MUPF::Alpha(0.5); 
const double MUPF::Beta(0.5); 

const time::milliseconds MUPF::RETX_SUPPRESSION_INITIAL(10);
const time::milliseconds MUPF::RETX_SUPPRESSION_MAX(250);

MUPF::MUPF(Forwarder& forwarder, const Name& name)
    : Strategy(forwarder),
      ProcessNackTraits(this),
    //   m_Rth(100.0),
      m_nodes(ns3::NodeContainer::GetGlobal()),
      m_retxSuppression(RETX_SUPPRESSION_INITIAL,
                        RetxSuppressionExponential::DEFAULT_MULTIPLIER,
                        RETX_SUPPRESSION_MAX) {
    ParsedInstanceName parsed = parseInstanceName(name);
    if (!parsed.parameters.empty()) {
        NDN_THROW(std::invalid_argument("MUPF does not accept parameters"));
    }
    if (parsed.version &&
        *parsed.version != getStrategyName()[-1].toVersion()) {
        NDN_THROW(std::invalid_argument("MUPF does not support version " +
                                        to_string(*parsed.version)));
    }
    this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

const Name& MUPF::getStrategyName() {
    static Name strategyName("/localhost/nfd/strategy/MUPF/%FD%01");
    return strategyName;
}


void MUPF::afterReceiveInterest(const FaceEndpoint& ingress,
                               const Interest& interest,
                               const shared_ptr<pit::Entry>& pitEntry)
{
    const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
    const Name prefix = fibEntry.getPrefix();
    const fib::NextHopList& nexthops = fibEntry.getNextHops();
    auto it = *nexthops.begin();

    /*
     *当到达Producer时，直接交付给App，不执行泛洪检索；
     *因为创建无线face时在FIB中添加了“/”的路由，因此匹配到“/”等价于FIB中无匹配项
    */
    if ( it.getFace().getId() != 256+m_nodes.GetN() && prefix == "/") {
        NFD_LOG_DEBUG("There is no entry in FIB, do Content Discovery!");
        contentDiscovery(ingress, interest, pitEntry);
        return;
    }

    it = this->selectFIB(nexthops);
	auto egress = FaceEndpoint(it.getFace(), 0);
    NFD_LOG_DEBUG("do Send Interest="<<interest << " from=" << ingress << "to=" << egress);
	this->sendInterest(pitEntry, egress, interest);
}

void
MUPF::beforeSatisfyInterest(const shared_ptr<pit::Entry>& pitEntry,
                        const FaceEndpoint& ingress, const Data& data) {

    NFD_LOG_DEBUG("beforeSatisfyInterest pitEntry=" << pitEntry->getName()
                << " in=" << ingress << " data=" << data.getName());
    
    const ndn::Name prefix = pitEntry->getName().getPrefix(1);
    bool isDiscovery = pitEntry->getInterest().getTag<lp::NonDiscoveryTag>() != nullptr;
    // 不是Content Discovery则不需要执行操作
    if ( !isDiscovery ) {return;}

    // 若是Content Discovery包，则需要延长pitEntry的生命周期，以等待不同上游的Data包返回
    this->setExpiryTimer(pitEntry, 5000_ms);
    
    ns3::Ptr<ns3::Node> localNode = getNode(*this);

    // 当Interest到达Producer后，将Provider ID添加到Data包的`CongestionMarkTag`中。
    if (ingress.face.getId()==256+m_nodes.GetN()) {
        NFD_LOG_DEBUG("Set Provider ID Tag="<<localNode->GetId());
        data.setTag(make_shared<lp::CongestionMarkTag>(localNode->GetId()));
    }

    // 当Data返回到请求的Consumer端时，结束泛洪检索，并触发路径建立过程。
    uint64_t requesterId = pitEntry->getInterest().getTag<lp::CongestionMarkTag>()->get();
    if (requesterId == localNode->GetId()) {
        NFD_LOG_DEBUG("Content Discovery Finished!");
        uint64_t providerNodeId = data.getTag<lp::CongestionMarkTag>()->get();
        ns3::Ptr<ns3::Node> providerNode = m_nodes[providerNodeId];
        this->unicastPathBuilding(prefix, localNode, providerNode);
    } 
}

void
MUPF::afterContentStoreHit(const shared_ptr<pit::Entry>& pitEntry,
                               const FaceEndpoint& ingress, const Data& data)
{
  NFD_LOG_DEBUG("afterContentStoreHit pitEntry=" << pitEntry->getName()
                << " in=" << ingress << " data=" << data.getName());

  this->sendData(pitEntry, data, ingress);
}

void
MUPF::afterReceiveData(const shared_ptr<pit::Entry>& pitEntry,
                           const FaceEndpoint& ingress, const Data& data)
{
  	this->beforeSatisfyInterest(pitEntry, ingress, data);
    NFD_LOG_DEBUG("do Receive Data pitEntry=" << pitEntry->getName()
                << " in=" << ingress << " data=" << data.getName());
	this->sendDataToAll(pitEntry, ingress, data);
}

void
MUPF::contentDiscovery(const FaceEndpoint& ingress, const Interest& interest, const shared_ptr<pit::Entry> &pitEntry) {
    const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
    const Name prefix = fibEntry.getPrefix();
    const fib::NextHopList& nexthops = fibEntry.getNextHops();
    const auto transport = nexthops.begin()->getFace().getTransport();
    ns3::ndn::WifiNetDeviceTransport *wifiTrans = dynamic_cast<ns3::ndn::WifiNetDeviceTransport *>(transport);
    ns3::Ptr<ns3::Node> localNode = wifiTrans->GetNode();
    if (ingress.face.getId()==256+m_nodes.GetN()) {
        // 使用NonDiscoveryTag标识是否是用于Content Discovery的Interest包
        interest.setTag(make_shared<lp::NonDiscoveryTag>(lp::EmptyValue{}));
        // 使用Interest包的CongestionMarkTag标识Requseter ID
        interest.setTag(make_shared<lp::CongestionMarkTag>(localNode->GetId()));
        NFD_LOG_DEBUG("Set Requester ID="<<interest.getTag<lp::CongestionMarkTag>()->get());
    }

    for (const auto& nexthop : nexthops) {
        uint32_t othNodeId = (nexthop.getFace().getId() - 257) + (localNode->GetId() + 257 <= nexthop.getFace().getId());
        ns3::Ptr<ns3::Node>othNode = m_nodes[othNodeId];
        if ( ingress.face.getId() == nexthop.getFace().getId() ||  !isInRegion(localNode, othNode) ) { continue; }
        auto egress = FaceEndpoint(nexthop.getFace(), 0);
        this->sendInterest(pitEntry, egress, interest);
        NFD_LOG_DEBUG("do Send Interest="<<interest << " from=" << ingress << "to=" << egress);
    }
}

void
MUPF::unicastPathBuilding(const ndn::Name prefix, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> providerNode) {
    // 循环执行直到Producer在当前relay的范围内
    while ( !isInRegion(srcNode, providerNode) ) {
        std::vector<MUPF::weightTableEntry> weightTable;
        for (const auto& node : m_nodes) {
            if ( !isIntermediateNode(node, srcNode, providerNode)) { continue; }
            double dis = this->calculateDistance(node, srcNode, providerNode);
            double dir = this->calculateDirection(node, srcNode, providerNode);
            double den = this->calculateDensity(node);
            double score = Mu*dis + Phi*dir + Omega*den;
            weightTableEntry entry = {node, dis, dir, den, score};
            weightTable.push_back(entry);
        }
        ns3::Ptr<ns3::Node> selectNode = std::max_element(weightTable.begin(), weightTable.end(), [](const auto& a, const auto& b) { return a.Score<b.Score;})->node;
        ns3::Ptr<ns3::ndn::L3Protocol> ndn = selectNode->GetObject<ns3::ndn::L3Protocol>();
        uint32_t faceId = (selectNode->GetId()+256) + (selectNode->GetId() < srcNode->GetId());
        shared_ptr<Face> face = ndn->getFaceById(faceId);
        ns3::ndn::FibHelper::AddRoute(srcNode, prefix, face, 1);
        NFD_LOG_DEBUG("Add Route: Node="<<srcNode->GetId()<<"Prefix="<<prefix<<"Face="<<faceId);
        srcNode = selectNode;
    }
    ns3::Ptr<ns3::ndn::L3Protocol> ndn = srcNode->GetObject<ns3::ndn::L3Protocol>();
    uint32_t faceId = (providerNode->GetId()+256) + (providerNode->GetId() < srcNode->GetId());
    shared_ptr<Face> face = ndn->getFaceById(faceId);
    ns3::ndn::FibHelper::AddRoute(srcNode, prefix, face, 1);
    NFD_LOG_DEBUG("Add Route: Node="<<srcNode->GetId()<<", Prefix="<<prefix<<", Face="<<faceId);
}

nfd::fib::NextHop
MUPF::selectFIB(const fib::NextHopList& nexthops) {
    const auto transport = nexthops.begin()->getFace().getTransport();
    ns3::ndn::WifiNetDeviceTransport *wifiTrans = dynamic_cast<ns3::ndn::WifiNetDeviceTransport *>(transport);
    auto selectedHop = *nexthops.begin();
    if (selectedHop.getFace().getId() == 256+m_nodes.GetN()) {return selectedHop;}
    ns3::Ptr<ns3::Node> localNode = wifiTrans->GetNode();
    double highestValue = 0.0;
    for (const auto& nexthop : nexthops) { 
        uint32_t faceId = nexthop.getFace().getId();
        uint32_t othNodeId = (faceId - 257) + (localNode->GetId() + 257 <= faceId);
        ns3::Ptr<ns3::Node>othNode = m_nodes[othNodeId];
        double let = this->calculateLET(localNode, othNode);
        double link_prob = this->calculateLAP(let, 2.0);
        double final_value = Alpha*let + Beta*link_prob;
        if (final_value > highestValue) {
            highestValue = final_value;
            selectedHop = nexthop;
        }
    }
    NFD_LOG_DEBUG("Selected Next Hop = "<<selectedHop.getFace().getId());
    return selectedHop;
}

bool
MUPF::isIntermediateNode(ns3::Ptr<ns3::Node> node, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> desNode) {
    double d_sd = ns3::CalculateDistance(srcNode->GetObject<ns3::MobilityModel>()->GetPosition(), desNode->GetObject<ns3::MobilityModel>()->GetPosition());
    double R2 = 1.5*d_sd - Rth;
    double d_sj = ns3::CalculateDistance(srcNode->GetObject<ns3::MobilityModel>()->GetPosition(), node->GetObject<ns3::MobilityModel>()->GetPosition());
    double d_jd = ns3::CalculateDistance(desNode->GetObject<ns3::MobilityModel>()->GetPosition(), node->GetObject<ns3::MobilityModel>()->GetPosition());
    if (d_sj <= Rth && d_jd <= R2) {
        return true;
    }
    return false;
}

double
MUPF::calculateDistance(ns3::Ptr<ns3::Node> localNode, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> desNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = localNode->GetObject<ns3::MobilityModel>();
    ns3::Ptr<ns3::MobilityModel> mobility2 = srcNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility3 = desNode->GetObject<ns3::MobilityModel>();
	double d_jd =  mobility1->GetDistanceFrom(mobility3);
	double d_sd =  mobility2->GetDistanceFrom(mobility3);
	return std::max(log(d_sd / d_jd), 0.1);
}

double
MUPF::calculateDirection(ns3::Ptr<ns3::Node> node, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> desNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = node->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = srcNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility3 = desNode->GetObject<ns3::MobilityModel>();
    ns3::Vector a = mobility1->GetVelocity();
    ns3::Vector b = mobility2->GetPosition() - mobility3->GetPosition();
    double dir = (a.x * b.x + a.y * b.y) / (a.GetLength() * b.GetLength());
    return dir;
}

double
MUPF::calculateDensity(ns3::Ptr<ns3::Node> node){
    int num_avg = 0;
    for (const auto& oth_node : m_nodes) {
        double d = ns3::CalculateDistance(node->GetObject<ns3::MobilityModel>()->GetPosition(), oth_node->GetObject<ns3::MobilityModel>()->GetPosition());
        if (d < Rth) {
            num_avg += 1;
        }
    }
    return num_avg;
}

double
MUPF::calculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = revNode->GetObject<ns3::MobilityModel>();
    double m = mobility1->GetPosition().x - mobility2->GetPosition().x;
    double n = mobility1->GetPosition().y - mobility2->GetPosition().y;
    double p = mobility1->GetVelocity().x - mobility2->GetVelocity().x + 0.0001;
    double q = mobility1->GetVelocity().y - mobility2->GetVelocity().y + 0.0001;
    double let = (-(m*p+n*q)+sqrt((pow(p,2)+pow(q,2))*pow(Rth,2) - pow(n*p-m*q, 2)) ) / (pow(p,2)+pow(q,2));
    return let;
}

double
MUPF::calculateLAP(double t, double delta_t) {
    double lambda = 10, eplison=0;
    double L = (1.0-exp(-2*lambda*t)) * (1.0/(2*lambda*t) + eplison) + 0.5*lambda*t*exp(-2*lambda*t);
    double prob = delta_t <= t ?  (1.0-(1.0-L)/t * delta_t) : L/(log(delta_t-t+1)+1);
    return prob;
}

bool
MUPF::isInRegion(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> recvNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = recvNode->GetObject<ns3::MobilityModel>();
    double distance = mobility1->GetDistanceFrom(mobility2);
    return distance <= Rth;
}

ns3::Ptr<ns3::Node>
MUPF::getNode(MUPF& local_strategy) {
    ns3::Ptr<ns3::Node> localNode;
    for(auto& node : m_nodes) {
        ns3::Ptr<ns3::ndn::L3Protocol> ndn = node->GetObject<ns3::ndn::L3Protocol>();
        ndn::Name prefix("/");
        nfd::fw::Strategy& strategy = ndn->getForwarder()->getStrategyChoice().findEffectiveStrategy(prefix);
        nfd::fw::MUPF& mupf_strategy = dynamic_cast<nfd::fw::MUPF&>(strategy);
        if ( &local_strategy == &mupf_strategy ) {
            localNode =node;
        }
    }
    return localNode;
}

}  // namespace fw
}  // namespace nfd