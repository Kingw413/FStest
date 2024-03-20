#include "lsif.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/algorithm.hpp"
#include "common/logger.hpp"
#include "ndn-wifi-net-device-transport-broadcast.hpp"
#include "ns3/mobility-model.h"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/apps/ndn-producer.hpp"
#include "ns3/simulator.h"

namespace nfd{
namespace fw{

NFD_REGISTER_STRATEGY(LSIF);
NFD_LOG_INIT(LSIF);

const time::milliseconds LSIF::RETX_SUPPRESSION_INITIAL(10);
const time::milliseconds LSIF::RETX_SUPPRESSION_MAX(250);

LSIF::LSIF(Forwarder &forwarder, const Name &name)
	: Strategy(forwarder), ProcessNackTraits(this),
	  m_retxSuppression(RETX_SUPPRESSION_INITIAL, RetxSuppressionExponential::DEFAULT_MULTIPLIER,
						RETX_SUPPRESSION_MAX),
	  m_forwarder(forwarder),
	  m_nodes(ns3::NodeContainer::GetGlobal()), m_Rth(200.0), m_LET_alpha(10.0)
{
	ParsedInstanceName parsed = parseInstanceName(name);
	if (!parsed.parameters.empty())
	{
		NDN_THROW(std::invalid_argument("LSIF does not accept parameters"));
	}
	if (parsed.version && *parsed.version != getStrategyName()[-1].toVersion())
	{
		NDN_THROW(std::invalid_argument(
			"LSIF does not support version " + to_string(*parsed.version)));
	}
	this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

const Name &
LSIF::getStrategyName()
{
	static Name strategyName("/localhost/nfd/strategy/LSIF/%FD%01");
	return strategyName;
}

void LSIF::afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
								const shared_ptr<pit::Entry> &pitEntry)
{
	const fib::Entry &fibEntry = this->lookupFib(*pitEntry);
	const fib::NextHopList &nexthops = fibEntry.getNextHops();
	auto it = nexthops.end();

	it = std::find_if(nexthops.begin(), nexthops.end(), [&](const auto &nexthop)
					  { return isNextHopEligible(ingress.face, interest, nexthop, pitEntry); });

	auto egress = FaceEndpoint(it->getFace(), 0);
	// 如果是Consumer端或Producer端，则直接转发给应用层，无需等待
	if (ingress.face.getId() == 256+m_nodes.GetN() || egress.face.getId() == 256+m_nodes.GetN()) {
		NFD_LOG_INFO("do Send Interest" << interest << " from=" << ingress << " to=" << egress);
		this->sendInterest(pitEntry, egress, interest);
        return;
	}

    const auto transport = ingress.face.getTransport();
    ns3::ndn::WifiNetDeviceTransportBroadcast *wifiTrans = dynamic_cast<ns3::ndn::WifiNetDeviceTransportBroadcast *>(transport);
    ns3::Ptr<ns3::Node> receiveNode = wifiTrans->GetNode();
    // 节点创建的face是从257开始依据节点序号依次递增的，据此计算face对端节点的序号
    int sendNodeId = (ingress.face.getId() - 257) + (receiveNode->GetId() + 257 <= ingress.face.getId());
    ns3::Ptr<ns3::Node> sendNode = m_nodes[sendNodeId];
    double LET = caculateLET(sendNode, receiveNode);
    if (LET < m_LET_alpha) {
        NS_LOG_DEBUG("LET < alpha, Cancel to forward");
		this->setExpiryTimer(pitEntry, 0_ms);
        return;
    }
    NFD_LOG_INFO("do Send Interest" << interest << " from=" << ingress << " to=" << egress);
    this->sendInterest(pitEntry, egress, interest);
}

void
LSIF::afterContentStoreHit(const shared_ptr<pit::Entry> &pitEntry,
								const FaceEndpoint &ingress, const Data &data)
{
	NFD_LOG_DEBUG("afterContentStoreHit pitEntry=" << pitEntry->getName()
												   << " in=" << ingress << " data=" << data.getName());

	this->sendData(pitEntry, data, ingress);
	NFD_LOG_DEBUG("do Send Data=" << data.getName() << ", from=" << ingress);
}

void
LSIF::afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
							const FaceEndpoint &ingress, const Data &data)
{
	Interest interest = pitEntry->getInterest();
	const auto& inface =  (pitEntry->getInRecords().begin()->getFace());
    auto egress = FaceEndpoint(inface,0);
	this->sendData(pitEntry,data,egress);
    NFD_LOG_DEBUG("do Send Data="<<data.getName()<<", from="<<ingress<<", to="<<egress);

}

double
LSIF::caculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode) {
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

} // namespace fw
} // namespace nfd
