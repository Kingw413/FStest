#include "lisic.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/algorithm.hpp"
#include "common/logger.hpp"
#include "common/global.hpp"
#include "ndn-wifi-net-device-transport-broadcast.hpp"
#include "ns3/mobility-model.h"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/apps/ndn-producer.hpp"
#include "ns3/simulator.h"
#include <random>

namespace nfd{
namespace fw{

NFD_REGISTER_STRATEGY(LISIC);
NFD_LOG_INIT(LISIC);

const time::milliseconds LISIC::RETX_SUPPRESSION_INITIAL(10);
const time::milliseconds LISIC::RETX_SUPPRESSION_MAX(250);

LISIC::LISIC(Forwarder &forwarder, const Name &name)
	: Strategy(forwarder), ProcessNackTraits(this),
	  m_retxSuppression(RETX_SUPPRESSION_INITIAL, RetxSuppressionExponential::DEFAULT_MULTIPLIER,
						RETX_SUPPRESSION_MAX),
	  m_forwarder(forwarder),
	  m_nodes(ns3::NodeContainer::GetGlobal()), m_Rth(200.0), m_alpha(1.0e9)
{
	ParsedInstanceName parsed = parseInstanceName(name);
	if (!parsed.parameters.empty())
	{
		NDN_THROW(std::invalid_argument("LISIC does not accept parameters"));
	}
	if (parsed.version && *parsed.version != getStrategyName()[-1].toVersion())
	{
		NDN_THROW(std::invalid_argument(
			"LISIC does not support version " + to_string(*parsed.version)));
	}
	this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

const Name &
LISIC::getStrategyName()
{
	static Name strategyName("/localhost/nfd/strategy/LISIC/%FD%01");
	return strategyName;
}

void LISIC::afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
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
		NS_LOG_DEBUG("Wait="<<deferTime<<"(s) to send Interest=" << interest << " from=" << ingress << " to=" << egress);
		auto eventId = ns3::Simulator::Schedule(ns3::Seconds(deferTime), &LISIC::doSend, this, pitEntry, egress, ingress, interest);
		this->addEntry(interest.getName(), interest.getNonce(), ns3::Seconds(deferTime), eventId);
	}
}

void
LISIC::afterReceiveLoopedInterest(const FaceEndpoint& ingress, const Interest& interest,
                             pit::Entry& pitEntry) {
	// NFD_LOG_DEBUG("afterReceiveLoopedInterest Interest=" << interest<< " in=" << ingress);
	auto it = findEntry(interest.getName(), interest.getNonce());
	if (it != m_waitTable.end()) {
		this->cancelSend(interest, it->eventId);
		this->deleteEntry(it);
		// 取消发送后删除对应的PIT表项（实际上下面的代码好像并不能完成这个目的）
		shared_ptr<pit::Entry> newpitEntry = m_forwarder.getPit().find(interest);
		this->setExpiryTimer(newpitEntry, 0_ms);
		// NFD_LOG_DEBUG("InRecords="<<pitEntry.getInRecords().size());
		// newpitEntry->expiryTimer.cancel();
		// m_forwarder.getPit().erase(newpitEntry.get());
	}
}

void LISIC::doSend(const shared_ptr<pit::Entry> &pitEntry,
				  const FaceEndpoint &egress, const FaceEndpoint &ingress,
				  const Interest &interest)
{
	NFD_LOG_INFO("do Send Interest " << interest << " from=" << ingress << " to=" << egress);
	this->sendInterest(pitEntry, egress, interest);
	auto it = findEntry(interest.getName(), interest.getNonce());
	this->deleteEntry(it);
}

void 
LISIC::afterContentStoreHit(const shared_ptr<pit::Entry> &pitEntry,
								const FaceEndpoint &ingress, const Data &data)
{
	NFD_LOG_DEBUG("afterContentStoreHit pitEntry=" << pitEntry->getName()
												   << " in=" << ingress << " data=" << data.getName());

	this->sendData(pitEntry, data, ingress);
	NFD_LOG_DEBUG("do Send Data=" << data.getName() << ", from=" << ingress);
}

void LISIC::afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
							const FaceEndpoint &ingress, const Data &data)
{
	if (pitEntry->getOutRecords().size() == 0)
	{
		// NFD_LOG_DEBUG("pitEntry no OutRecords");
		return;
	}
	const auto &inface = (pitEntry->getInRecords().begin()->getFace());
	auto egress = FaceEndpoint(inface,0);
	this->sendData(pitEntry,data,egress);
    NFD_LOG_DEBUG("do Send Data="<<data.getName()<<", from="<<ingress<<", to="<<egress);
}

std::vector<LISIC::m_tableEntry>::iterator
LISIC::findEntry(const Name& name, uint32_t nonce) {
    return std::find_if(m_waitTable.begin(), m_waitTable.end(),
                        [&](const m_tableEntry& entry) {
                          return entry.interestName == name && entry.nonce == nonce;
                        });
  }

void
LISIC::addEntry(const Name &interestName, uint32_t nonce, ns3::Time deferTime, ns3::EventId eventId)
{
	m_tableEntry newEntry(interestName, nonce, deferTime, eventId);
	m_waitTable.push_back(newEntry);
	// NFD_LOG_DEBUG("Add WaitTable Entry: ("<<interestName <<", "<<nonce<<", " <<deferTime.GetSeconds()<<", " <<eventId.GetUid()<<")");
}

void
LISIC::deleteEntry(std::vector<LISIC::m_tableEntry>::iterator it)
{
	if (it != m_waitTable.end()) {
		// NFD_LOG_DEBUG("Delete WaitTable Entry: ("<< it->interestName <<", "<< it->nonce<<", " << it->deferTime.GetSeconds()<<", " << it->eventId.GetUid()<<")");
		m_waitTable.erase(it);
	}
}

double
LISIC::caculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode) {
	if (sendNode->GetObject<ns3::MobilityModel>()->GetDistanceFrom(revNode->GetObject<ns3::MobilityModel>()) >m_Rth ) { return 0;}
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
LISIC::caculateDeferTime(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode) {
    double let = this->caculateLET(sendNode, receiveNode);
    std::random_device rd;
    std::mt19937 gen(rd());
    double T_random = std::uniform_real_distribution<double>(0, 0.1) (gen);
    double defer_time = m_alpha * pow(2*m_Rth/3/1e8, 2) / let *(1+T_random) + m_Rth/3/1e8;
	return defer_time;
}

void
LISIC::cancelSend(Interest interest, ns3::EventId eventId) {
	ns3::Simulator::Cancel(eventId);
	// NS_LOG_DEBUG("Cancel Forwarding Interest: "<<interest);
}

} // namespace fw
} // namespace nfd
