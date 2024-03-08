#include "vndn.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/algorithm.hpp"
#include "common/logger.hpp"
#include "ndn-wifi-net-device-transport-broadcast.hpp"
#include "ns3/mobility-model.h"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/apps/ndn-producer.hpp"
#include "ns3/simulator.h"
#include <random>

namespace nfd{
namespace fw{

NFD_REGISTER_STRATEGY(VNDN);
NFD_LOG_INIT(VNDN);

const time::milliseconds VNDN::RETX_SUPPRESSION_INITIAL(10);
const time::milliseconds VNDN::RETX_SUPPRESSION_MAX(250);

VNDN::VNDN(Forwarder &forwarder, const Name &name)
	: Strategy(forwarder), 
	  m_retxSuppression(RETX_SUPPRESSION_INITIAL, RetxSuppressionExponential::DEFAULT_MULTIPLIER,
						RETX_SUPPRESSION_MAX),
	  m_nodes(ns3::NodeContainer::GetGlobal()), m_Rth(200.0)
{
	ParsedInstanceName parsed = parseInstanceName(name);
	if (!parsed.parameters.empty())
	{
		NDN_THROW(std::invalid_argument("VNDN does not accept parameters"));
	}
	if (parsed.version && *parsed.version != getStrategyName()[-1].toVersion())
	{
		NDN_THROW(std::invalid_argument(
			"VNDN does not support version " + to_string(*parsed.version)));
	}
	this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

const Name &
VNDN::getStrategyName()
{
	static Name strategyName("/localhost/nfd/strategy/VNDN/%FD%01");
	return strategyName;
}

void VNDN::afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
								const shared_ptr<pit::Entry> &pitEntry)
{
	const fib::Entry &fibEntry = this->lookupFib(*pitEntry);
	const fib::NextHopList &nexthops = fibEntry.getNextHops();
	auto it = nexthops.end();
	it = std::find_if(nexthops.begin(), nexthops.end(), [&](const auto &nexthop)
					  { return isNextHopEligible(ingress.face, interest, nexthop, pitEntry); });

	if (it == nexthops.end()) {
		NFD_LOG_DEBUG(interest << " from=" << ingress << " noNextHop");
		return;
	}

	auto egress = FaceEndpoint(it->getFace(), 0);
	// 如果是Consumer端或Producer端，则直接转发给应用层，无需等待
	if (ingress.face.getId() == 256+m_nodes.GetN() || egress.face.getId() == 256+m_nodes.GetN()) {
		NFD_LOG_INFO("do Send Interest " << interest << " from=" << ingress << " to=" << egress);
		this->sendInterest(pitEntry, egress, interest);
	}
	else {
		// 特殊情况：pitEntry到达TTL被删除，因此不会触发afterReceiveLoopedInterest()，但此时WT中还存在等待转发的表项
		auto it = findEntry(interest.getName(), interest.getNonce());
		if (it != m_waitTable.end()) {
			this->cancelSend(interest, it->eventId);
			this->deleteEntry(it);
			// 取消发送后删除对应的PIT表项
			this->setExpiryTimer(pitEntry, 0_ms);
			return;
		}
		const auto transport = ingress.face.getTransport();
		ns3::ndn::WifiNetDeviceTransportBroadcast* wifiTrans = dynamic_cast<ns3::ndn::WifiNetDeviceTransportBroadcast*>(transport);
		ns3::Ptr<ns3::Node> receiveNode = wifiTrans->GetNode();
		// 节点创建的face是从257开始依据节点序号依次递增的，据此计算face对端节点的序号
		int sendNodeId = (ingress.face.getId() - 257) + (receiveNode->GetId()+257 <= ingress.face.getId());
		ns3::Ptr<ns3::Node> sendNode = m_nodes[sendNodeId];
		double deferTime = caculateDeferTime(sendNode, receiveNode);
		NS_LOG_DEBUG("Wait "<<deferTime<<"s to send Interest=" << interest << " from=" << ingress << " to=" << egress);
		auto eventId = ns3::Simulator::Schedule(ns3::Seconds(deferTime), &VNDN::doSend, this, pitEntry, egress, ingress, interest);
		this->addEntry(interest.getName(), interest.getNonce(), ns3::Seconds(deferTime), eventId);
	}
	return;
}

void
VNDN::afterReceiveLoopedInterest(const FaceEndpoint& ingress, const Interest& interest,
                             pit::Entry& pitEntry) {
	NFD_LOG_DEBUG("afterReceiveLoopedInterest Interest=" << interest<< " in=" << ingress);
	auto it = findEntry(interest.getName(), interest.getNonce());
	if (it != m_waitTable.end()) {
		this->cancelSend(interest, it->eventId);
		this->deleteEntry(it);
		// 取消发送后删除对应的PIT表项
		std::shared_ptr<nfd::pit::Entry> sharedPitEntry(&pitEntry, [] (nfd::pit::Entry*) {});
		this->setExpiryTimer(sharedPitEntry, 0_ms);
	}
}

void VNDN::doSend(const shared_ptr<pit::Entry> &pitEntry,
				  const FaceEndpoint &egress, const FaceEndpoint &ingress,
				  const Interest &interest)
{
	NFD_LOG_INFO("do Send Interest" << interest << " from=" << ingress << " to=" << egress);
	this->sendInterest(pitEntry, egress, interest);
	auto it = findEntry(interest.getName(), interest.getNonce());
	this->deleteEntry(it);
}

void VNDN::afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
							const FaceEndpoint &ingress, const Data &data)
{
	Interest interest = pitEntry->getInterest();
	// auto it = findEntry(interest.getName(), interest.getNonce());
	// if (it != m_waitTable.end()) {
	// 	this->cancelSend(interest, it->eventId);
	// 	this->deleteEntry(it);
	// }
	const auto& inface =  (pitEntry->getInRecords().begin()->getFace());
    auto egress = FaceEndpoint(inface,0);
    NFD_LOG_DEBUG("do Send Data="<<data.getName()<<", from="<<ingress<<", to="<<egress);
	this->sendData(pitEntry,data,egress);
}

void
VNDN::cancelSend(Interest interest, ns3::EventId eventId) {
	ns3::Simulator::Cancel(eventId);
	NS_LOG_DEBUG("Cancel Forwarding Interest: "<<interest);
}

double
VNDN::caculateDeferTime(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode) {
	ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = receiveNode->GetObject<ns3::MobilityModel>();
	double distance =  mobility2->GetDistanceFrom(mobility1);
	std::random_device rd;
    std::mt19937 gen(rd());
    double T_random = std::uniform_real_distribution<double>(0, 0.1) (gen);
	double defer_time = 1/(distance+0.0001) * (1+T_random);
	return defer_time;
}

std::vector<VNDN::m_tableEntry>::iterator
VNDN::findEntry(const Name& name, uint32_t nonce) {
    return std::find_if(m_waitTable.begin(), m_waitTable.end(),
                        [&](const m_tableEntry& entry) {
                          return entry.interestName == name && entry.nonce == nonce;
                        });
  }

void
VNDN::addEntry(const Name &interestName, uint32_t nonce, ns3::Time deferTime, ns3::EventId eventId)
{
	m_tableEntry newEntry(interestName, nonce, deferTime, eventId);
	m_waitTable.push_back(newEntry);
	// NFD_LOG_DEBUG("Add WaitTable Entry: ("<<interestName <<", "<<nonce<<", " <<deferTime.GetSeconds()<<", " <<eventId.GetUid()<<")");
}

void
VNDN::deleteEntry(std::vector<VNDN::m_tableEntry>::iterator it)
{
	if (it != m_waitTable.end()) {
		// NFD_LOG_DEBUG("Delete WaitTable Entry: ("<< it->interestName <<", "<< it->nonce<<", " << it->deferTime.GetSeconds()<<", " << it->eventId.GetUid()<<")");
		m_waitTable.erase(it);
	}
}

} // namespace fw
} // namespace nfd