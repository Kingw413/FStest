#include "vndn.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/algorithm.hpp"
#include "common/logger.hpp"
#include "ndn-wifi-net-device-transport.hpp"
#include "ns3/mobility-model.h"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/apps/ndn-producer.hpp"
#include "ns3/simulator.h"

namespace nfd{
namespace fw{

NFD_REGISTER_STRATEGY(VNDN);
NFD_LOG_INIT(VNDN);

const time::milliseconds VNDN::RETX_SUPPRESSION_INITIAL(10);
const time::milliseconds VNDN::RETX_SUPPRESSION_MAX(250);

VNDN::VNDN(Forwarder &forwarder, const Name &name)
	: Strategy(forwarder), ProcessNackTraits(this),
	  m_retxSuppression(RETX_SUPPRESSION_INITIAL, RetxSuppressionExponential::DEFAULT_MULTIPLIER,
						RETX_SUPPRESSION_MAX),
	  m_forwarder(forwarder),
	  m_nodes(ns3::NodeContainer::GetGlobal()), m_Rth(100)
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
		lp::NackHeader nackHeader;
		nackHeader.setReason(lp::NackReason::NO_ROUTE);
		this->sendNack(pitEntry, ingress, nackHeader);
		this->rejectPendingInterest(pitEntry);
		return;
	}

	auto egress = FaceEndpoint(it->getFace(), 0);
	// 如果是Consumer端或Producer端，则直接转发给应用层，无需等待
	if (ingress.face.getId() == 256+m_nodes.GetN() || egress.face.getId() == 256+m_nodes.GetN()) {
		NFD_LOG_INFO("do send " << interest << " from=" << ingress << " to=" << egress);
		this->sendInterest(pitEntry, egress, interest);
	}
	else {
		// 特殊情况：pitEntry到达TTL被删除，因此不会触发afterReceiveLoopedInterest()，但此时WT中还存在等待转发的表项
		auto it = findEntry(interest.getName(), interest.getNonce());
		if (it != m_waitTable.end()) {
			this->cancelSend(interest, it->eventId);
			this->deleteEntry(it);
			return;
		}
		const auto transport = ingress.face.getTransport();
		ns3::ndn::WifiNetDeviceTransport* wifiTrans = dynamic_cast<ns3::ndn::WifiNetDeviceTransport*>(transport);
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
	}
}

void VNDN::doSend(const shared_ptr<pit::Entry> &pitEntry,
				  const FaceEndpoint &egress, const FaceEndpoint &ingress,
				  const Interest &interest)
{
	NFD_LOG_INFO("do send" << interest << " from=" << ingress << " to=" << egress);
	this->sendInterest(pitEntry, egress, interest);
	auto it = findEntry(interest.getName(), interest.getNonce());
	this->deleteEntry(it);
}

void VNDN::afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
							const FaceEndpoint &ingress, const Data &data)
{
	NFD_LOG_DEBUG("afterReceiveData Interest=" << pitEntry->getInterest().getName()<<" Nonce="<<pitEntry->getInterest().getNonce()<< " in=" << ingress);
	Interest interest = pitEntry->getInterest();
	// auto it = findEntry(interest.getName(), interest.getNonce());
	// if (it != m_waitTable.end()) {
	// 	this->cancelSend(interest, it->eventId);
	// 	this->deleteEntry(it);
	// }
	const auto& inface =  (pitEntry->getInRecords().begin()->getFace());
    auto egress = FaceEndpoint(inface,0);
	this->sendData(pitEntry,data,egress);
	// this->sendDataToAll(pitEntry, ingress, data);
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
	NFD_LOG_DEBUG("Add WaitTable Entry: ("<<interestName <<", "<<nonce<<", " <<deferTime.GetSeconds()<<", " <<eventId.GetUid()<<")");
}

void
VNDN::deleteEntry(std::vector<VNDN::m_tableEntry>::iterator it)
{
	if (it != m_waitTable.end()) {
		NFD_LOG_DEBUG("Delete WaitTable Entry: ("<< it->interestName <<", "<< it->nonce<<", " << it->deferTime.GetSeconds()<<", " << it->eventId.GetUid()<<")");
		m_waitTable.erase(it);
	}
}

double
VNDN::caculateDeferTime(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode) {
	ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = receiveNode->GetObject<ns3::MobilityModel>();
	double distance =  mobility2->GetDistanceFrom(mobility1);
	double defer_time = 1/(distance+0.0001);
	return defer_time;
}

void
VNDN::cancelSend(Interest interest, ns3::EventId eventId) {
	ns3::Simulator::Cancel(eventId);
	NS_LOG_DEBUG("Cancel Forwarding Interest: "<<interest);
}

bool
VNDN::isInRegion(const FaceEndpoint &ingress) {
    const auto transport = ingress.face.getTransport();
    ns3::ndn::WifiNetDeviceTransport *wifiTrans = dynamic_cast<ns3::ndn::WifiNetDeviceTransport *>(transport);
	if (wifiTrans==nullptr) {
		return true;
	}
    ns3::Ptr<ns3::Node> receiveNode = wifiTrans->GetNode();
    // 节点创建的face是从257开始依据节点序号依次递增的，据此计算face对端节点的序号
    int sendNodeId = (ingress.face.getId() - 257) + (receiveNode->GetId() + 257 <= ingress.face.getId());
    ns3::Ptr<ns3::Node> sendNode = m_nodes[sendNodeId];
    ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = receiveNode->GetObject<ns3::MobilityModel>();
	double distance = mobility2->GetDistanceFrom(mobility1);
	return (distance<m_Rth);
}


/* 更新路径列表
 void
VNDN::updateHopList(nfd::face::Face& inface, nfd::face::Face& outface, const Interest& interest)
{
	if (outface.getId()<256+m_nodes.GetN()) {
		ns3::Ptr<ns3::Node> node;
		const auto transport = inface.getTransport();
		ns3::ndn::WifiNetDeviceTransport* wifiTrans = dynamic_cast<ns3::ndn::WifiNetDeviceTransport*>(transport);
		if(wifiTrans == nullptr) {
			const auto transport2 = outface.getTransport();
			ns3::ndn::WifiNetDeviceTransport* wifiTrans2 = dynamic_cast<ns3::ndn::WifiNetDeviceTransport*>(transport2);
			node = wifiTrans2->GetNetDevice()->GetNode();
		}
		else{
			node = wifiTrans->GetNetDevice()->GetNode();
		}
		int next_nodeId = outface.getId()-257 + (node->GetId()+257<=outface.getId());
		uint32_t nonce = interest.getNonce();
		ns3::Ptr<ns3::Node> next_node = m_nodes[next_nodeId];
		ndn::Name prefix("/");
		ns3::Ptr<ns3::ndn::L3Protocol> pre_ndn = node->GetObject<ns3::ndn::L3Protocol>();
		nfd::fw::Strategy& strategy1 = pre_ndn->getForwarder()->getStrategyChoice().findEffectiveStrategy(prefix);
		nfd::fw::VNDN& VNDN_strategy1 = dynamic_cast<nfd::fw::VNDN&>(strategy1);
		ns3::Ptr<ns3::ndn::L3Protocol> ndn = next_node->GetObject<ns3::ndn::L3Protocol>();
		nfd::fw::Strategy& strategy2 = ndn->getForwarder()->getStrategyChoice().findEffectiveStrategy(prefix);
		nfd::fw::VNDN& VNDN_strategy2 = dynamic_cast<nfd::fw::VNDN&>(strategy2);
		std::map<uint32_t, std::vector<int>>& pre_hop = VNDN_strategy1.getHOP();
		std::map<uint32_t, std::vector<int>>& hop = VNDN_strategy2.getHOP();
		VNDN_strategy2.setHopList(nonce, pre_hop, hop, node->GetId(), next_nodeId);
	}
}
*/

/* 设置路径
void
VNDN::setHopList(uint32_t nonce, std::map<uint32_t, std::vector<int>>& pre_hop, std::map<uint32_t, std::vector<int>>& hop, int hopId, int next_hopId) {
	if (pre_hop.find(nonce)==pre_hop.end()) {
		pre_hop[nonce] = {hopId};
	}
	hop[nonce] = pre_hop[nonce];
	hop[nonce].push_back(next_hopId);
	  std::ostringstream oss;
//   for (const auto& element : pre_hop[nonce]) {
//     oss << element << " ";
//   }
//     NFD_LOG_INFO("HopList: "<<oss.str());
}
*/

/* 计数跳数
void
VNDN::getHopCounts(const Interest& interest,
								 ns3::Ptr<ns3::Node> node)
{
	uint32_t nonce = interest.getNonce();
	ns3::Ptr<ns3::ndn::L3Protocol> ndn = node->GetObject<ns3::ndn::L3Protocol>();
	ndn::Name prefix("/");
	nfd::fw::Strategy& strategy = ndn->getForwarder()->getStrategyChoice().findEffectiveStrategy(prefix);
	nfd::fw::VNDN& VNDN_strategy = dynamic_cast<nfd::fw::VNDN&>(strategy);
	std::map<uint32_t, std::vector<int>>& hop = VNDN_strategy.getHOP();
	std::vector<int> hop_list = hop[nonce];
	std::ostringstream oss;
	for (const auto& element : hop_list) {
		oss << element << " ";
	}
	NFD_LOG_INFO("HopList: "<<oss.str());
	NS_LOG_INFO("Interest="<<interest.getName()<<" Nonce="<<nonce<<" HopCounts="<<hop_list.size()-1);
}
 */

} // namespace fw
} // namespace nfd
