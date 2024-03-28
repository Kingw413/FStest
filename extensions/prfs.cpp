#include "prfs.hpp"

#include "common/logger.hpp"
#include "ndn-wifi-net-device-transport-broadcast.hpp"
#include "ns3/mobility-model.h"
#include "ns3/ndnSIM/NFD/daemon/fw/algorithm.hpp"
#include "ns3/ndnSIM/model/ndn-net-device-transport.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/ptr.h"
#include "ns3/wifi-net-device.h"
#include "ns3/ndnSIM/apps/ndn-producer.hpp"
#include <cmath>

#include "ndn-cxx/interest.hpp"
namespace nfd {
namespace fw {

NFD_LOG_INIT(PRFS);
NFD_REGISTER_STRATEGY(PRFS);

const time::milliseconds PRFS::RETX_SUPPRESSION_INITIAL(10);
const time::milliseconds PRFS::RETX_SUPPRESSION_MAX(250);

PRFS::PRFS(Forwarder& forwarder, const Name& name)
    : Strategy(forwarder),
      ProcessNackTraits(this),
      m_Rth(200.0),
      m_LET_alpha(1.0),
      m_nodes(ns3::NodeContainer::GetGlobal()),
      m_retxSuppression(RETX_SUPPRESSION_INITIAL,
                        RetxSuppressionExponential::DEFAULT_MULTIPLIER,
                        RETX_SUPPRESSION_MAX) {
    ParsedInstanceName parsed = parseInstanceName(name);
    if (!parsed.parameters.empty()) {
        NDN_THROW(std::invalid_argument("PRFS does not accept parameters"));
    }
    if (parsed.version &&
        *parsed.version != getStrategyName()[-1].toVersion()) {
        NDN_THROW(std::invalid_argument("PRFS does not support version " +
                                        to_string(*parsed.version)));
    }
    this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

const Name& PRFS::getStrategyName() {
    static Name strategyName("/localhost/nfd/strategy/PRFS/%FD%01");
    return strategyName;
}


void PRFS::afterReceiveInterest(const FaceEndpoint& ingress,
                               const Interest& interest,
                               const shared_ptr<pit::Entry>& pitEntry)
{
    const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
    const fib::NextHopList& nexthops = fibEntry.getNextHops();

	auto it = std::find_if(nexthops.begin(), nexthops.end(), [&](const auto &nexthop)
					  { return isNextHopEligible(ingress.face, interest, nexthop, pitEntry); });
	auto egress = FaceEndpoint(it->getFace(), 0);

    const auto transport = ingress.face.getTransport();
    ns3::ndn::WifiNetDeviceTransportBroadcast* wifiTrans = dynamic_cast<ns3::ndn::WifiNetDeviceTransportBroadcast*>(transport);
    // 到达producer则直接转发给上层app
    if (egress.face.getId() == 256+m_nodes.GetN()) {
        this->sendInterest(pitEntry, egress, interest);
        // NFD_LOG_DEBUG("producer receive Interest="<<interest << " from=" << ingress << " to=" << egress);
        return;
    }

    // 转发流程核心
    bool isRelay = false;
     // consumer端在两个方向上转发
    if (wifiTrans == nullptr) {
        this -> setNextHop(nexthops, interest, pitEntry, true, true);
        isRelay = true;
    }
    else {
        ns3::Ptr<ns3::Node> node = wifiTrans->GetNode();
        int pre_node = (ingress.face.getId() - 257) + (node->GetId()+257 <= ingress.face.getId());
        ns3::Ptr<ns3::Node> preNode = m_nodes[pre_node];
    	ns3::Ptr<ns3::ndn::L3Protocol> ndn = preNode->GetObject<ns3::ndn::L3Protocol>();
		ndn::Name prefix("/");
		nfd::fw::Strategy& strategy =  ndn->getForwarder()->getStrategyChoice().findEffectiveStrategy(prefix);
    	nfd::fw::PRFS& prfs_strategy =  dynamic_cast<nfd::fw::PRFS&>(strategy);
        std::vector<inteAddField> intTable = prfs_strategy.m_IntTable;
        auto it = find_if(intTable.begin(), intTable.end(),
                        [&](const inteAddField& entry) {
                          return entry.name == interest.getName() && entry.nonce == interest.getNonce();
                        });
        if ( it->FIRD == node) {
            this->setNextHop(nexthops, interest, pitEntry, true, false);
            isRelay = true;
        }
        if (it->FIRRD == node) {
            this ->setNextHop(nexthops, interest, pitEntry, false, false);
            isRelay = true;
        }
    }
    if (isRelay) {
        NFD_LOG_DEBUG("do Send Interest="<<interest << " from=" << ingress << " to=" << egress);
	    this->sendInterest(pitEntry, egress, interest);
    }
    // 对于不是forwarder的节点，不应当创建pit，将其删除，防止后续收到Data包再广播，导致不必要的DATA冗余。
    else {
        this -> setExpiryTimer(pitEntry, 0_ms);
    }
}

void 
PRFS::afterContentStoreHit(const shared_ptr<pit::Entry> &pitEntry,
                                const FaceEndpoint &ingress, const Data &data)
{
    // NFD_LOG_DEBUG("afterContentStoreHit pitEntry=" << pitEntry->getName()
    //                                                << " in=" << ingress << " data=" << data.getName());

    this->sendData(pitEntry, data, ingress);
    NFD_LOG_DEBUG("do Send Data=" << data.getName() << ", from=" << ingress);
}

void
PRFS::afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
							const FaceEndpoint &ingress, const Data &data)
{
	Interest interest = pitEntry->getInterest();
	const auto& inface =  (pitEntry->getInRecords().begin()->getFace());
    // auto outface = pitEntry->getOutRecords().size();
    // if (!outface) {
    //     NFD_LOG_DEBUG("Non Forwarder receive Data, dont't forward");
    // }
    auto egress = FaceEndpoint(inface,0);
	this->sendData(pitEntry,data,egress);
    NFD_LOG_DEBUG("do Send Data="<<data.getName()<<", from="<<ingress<<", to="<<egress);
}

void
PRFS::setNextHop(const fib::NextHopList& nexthops,
                            const Interest& interest,
                            const shared_ptr<pit::Entry>& pitEntry,
                            bool  isRD,
                            bool isConsumer){

    const auto transport = nexthops.begin()->getFace().getTransport();
    ns3::ndn::WifiNetDeviceTransportBroadcast* wifiTrans = dynamic_cast<ns3::ndn::WifiNetDeviceTransportBroadcast*>(transport);
    ns3::Ptr<ns3::Node> node = wifiTrans->GetNode();
    
    ns3::Ptr<ns3::Node> FIRD = nullptr;
    ns3::Ptr<ns3::Node> FIRRD = nullptr;
    std::vector<ns3::Ptr<ns3::Node>> DL_RD;
    std::vector<ns3::Ptr<ns3::Node>> DL_RRD;
    for (auto hop = nexthops.begin(); hop != nexthops.end(); ++hop) {
        int remoteNodeId = (hop->getFace().getId() - 257) + (node->GetId()+257 <= hop->getFace().getId());
	    ns3::Ptr<ns3::Node> remoteNode = m_nodes[remoteNodeId];
        // 跳过在通信范围之外的节点
        if (calculateDistance(node, remoteNode) > m_Rth || calculateLET(node, remoteNode) < m_LET_alpha)
        {
            continue;
        }
        // 按照方向将节点分成两个集合
        if (isRoadDirection(node, remoteNode)) { 
            DL_RD.push_back(remoteNode);
        }
        else {
            DL_RRD.push_back(remoteNode);
        }
    }

    if (isConsumer || isRD) {
        double distance = 0;
        for (const auto &rd_node : DL_RD) {
            double newDistance = caculateDR(node, rd_node);
            if (newDistance > distance) {
                distance = newDistance;
                FIRD = rd_node;
            }
        }
        // if(FIRD) {
        //      NFD_LOG_DEBUG("FIRD="<<FIRD->GetId()<<", Dis="<<distance);
        // }
        // else{NFD_LOG_DEBUG("No Next FIRD");}
    }
    if (isConsumer || !isRD) {
        double distance = 0;
        for (const auto &rrd_node : DL_RRD) {
            double newDistance = caculateDR(node, rrd_node);
            if (newDistance > distance) {
                distance = newDistance;
                FIRRD = rrd_node;
            }
        }
        // if(FIRRD) {
        //     NFD_LOG_DEBUG("FIRRD=" << FIRRD->GetId() << ", Dis=" << distance);
        // }
        // else{NFD_LOG_DEBUG("No Next FIRRD");}
    }

    PRFS::inteAddField inteEntry(interest.getName(), interest.getNonce(), FIRD, FIRRD);
    m_IntTable.push_back(inteEntry);
}

bool
PRFS::isRoadDirection(ns3::Ptr<ns3::Node> node, ns3::Ptr<ns3::Node> remote_node) {
    ns3::Ptr<ns3::MobilityModel> mobility = node->GetObject<ns3::MobilityModel>();
    ns3::Vector3D nodePos = mobility->GetPosition();
    ns3::Ptr<ns3::MobilityModel> remoteMob = remote_node->GetObject<ns3::MobilityModel>();
    ns3::Vector3D remotePos = remoteMob->GetPosition();
    // ns3::Vector3D direction = remoteMob->GetVelocity();
    ns3::Vector3D direction = {1.0, 0.0, 0.0};
    // if (direction.x==0 && direction.y==0) { direction.x += 0.001; direction.y  +=0.001;}
    if ( (remotePos.x-nodePos.x) * (direction.x) + (remotePos.y-nodePos.y) * (direction.y) >= 0 ) {return true;}
    return false;
}

double
PRFS::calculateDistance(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = receiveNode->GetObject<ns3::MobilityModel>();
	double distance =  mobility2->GetDistanceFrom(mobility1);
	return distance;
}

double
PRFS::calculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = revNode->GetObject<ns3::MobilityModel>();
    double m = mobility1->GetPosition().x - mobility2->GetPosition().x;
    double n = mobility1->GetPosition().y - mobility2->GetPosition().y;
    double p = mobility1->GetVelocity().x - mobility2->GetVelocity().x;
    double q = mobility1->GetVelocity().y - mobility2->GetVelocity().y;
    if (p==0 && q==0) {return 1e6;} //相对速度为0时，用1e6表示无限大
    double let = (-(m*p+n*q)+sqrt((pow(p,2)+pow(q,2))*pow(m_Rth,2) - pow(n*p-m*q, 2)) ) / (pow(p,2)+pow(q,2));
    return let;
}

double
PRFS::caculateDR(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode) {
    double eculid = PRFS::calculateDistance(sendNode, receiveNode);
    ns3::Ptr<ns3::MobilityModel> mobility = sendNode->GetObject<ns3::MobilityModel>();
    ns3::Vector3D nodePos = mobility->GetPosition();
    ns3::Ptr<ns3::MobilityModel> remoteMob = receiveNode->GetObject<ns3::MobilityModel>();
    ns3::Vector3D remotePos = remoteMob->GetPosition();
    // ns3::Vector3D direction = remoteMob->GetVelocity();
    ns3::Vector3D direction = {1.0, 0.0, 0.0};
    double angle = std::atan2(direction.x, direction.y) - std::atan2( remotePos.x-nodePos.x, remotePos.y-nodePos.y);
    double dr = abs(eculid * cos(angle));
    return dr;
}

}  // namespace fw
}  // namespace nfd