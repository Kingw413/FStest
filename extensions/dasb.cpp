#include "dasb.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/algorithm.hpp"
#include "common/logger.hpp"
#include "ndn-wifi-net-device-transport-broadcast.hpp"
#include "ns3/mobility-model.h"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/apps/ndn-producer.hpp"
#include "ns3/simulator.h"

namespace nfd{
namespace fw{

NFD_REGISTER_STRATEGY(DASB);
NFD_LOG_INIT(DASB);

const time::milliseconds DASB::RETX_SUPPRESSION_INITIAL(10);
const time::milliseconds DASB::RETX_SUPPRESSION_MAX(250);
const double DASB::DEFER_TIME_MAX(2e-3);
const double DASB::TRANSMISSION_RANGE(500);
const double DASB::SUPPRESSION_ANGLE(acos(-1)/4);

DASB::DASB(Forwarder &forwarder, const Name &name)
	: Strategy(forwarder), ProcessNackTraits(this),
	    m_retxSuppression(RETX_SUPPRESSION_INITIAL, RetxSuppressionExponential::DEFAULT_MULTIPLIER,
						RETX_SUPPRESSION_MAX),
        m_Tm(DEFER_TIME_MAX), m_Rth(TRANSMISSION_RANGE), m_Angle(SUPPRESSION_ANGLE),
	    m_forwarder(forwarder),
	    m_nodes(ns3::NodeContainer::GetGlobal())
{
	ParsedInstanceName parsed = parseInstanceName(name);
	if (!parsed.parameters.empty())
	{
		NDN_THROW(std::invalid_argument("DASB does not accept parameters"));
	}
	if (parsed.version && *parsed.version != getStrategyName()[-1].toVersion())
	{
		NDN_THROW(std::invalid_argument(
			"DASB does not support version " + to_string(*parsed.version)));
	}
	this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

const Name &
DASB::getStrategyName()
{
	static Name strategyName("/localhost/nfd/strategy/DASB/%FD%01");
	return strategyName;
}

void DASB::afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
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
		NFD_LOG_INFO("do send" << interest << " from=" << ingress << " to=" << egress);
		this->sendInterest(pitEntry, egress, interest);
	}
	else {
		auto it = findEntry(interest.getName(), interest.getNonce(), m_waitTableInt);
        const auto transport = ingress.face.getTransport();
	    ns3::ndn::WifiNetDeviceTransportBroadcast* wifiTrans = dynamic_cast<ns3::ndn::WifiNetDeviceTransportBroadcast*>(transport);
	    ns3::Ptr<ns3::Node> receiveNode = wifiTrans->GetNode();
	    // 节点创建的face是从257开始依据节点序号依次递增的，据此计算face对端节点的序号
	    int sendNodeId = (ingress.face.getId() - 257) + (receiveNode->GetId()+257 <= ingress.face.getId());
	    ns3::Ptr<ns3::Node> sendNode = m_nodes[sendNodeId];
		if (it != m_waitTableInt.end() && shouldSuppress(it->preNode, sendNode, receiveNode)) {
			this->cancelSend(it->eventId);
			this->deleteEntry(it, m_waitTableInt);
            return;
		}
		NS_LOG_DEBUG("Wait to send " << interest << " from=" << ingress << " to=" << egress);
		double deferTime = calculateDeferTime(sendNode, receiveNode);
		auto eventId = ns3::Simulator::Schedule(ns3::Seconds(deferTime), &DASB::doSendInterest, this, pitEntry, egress, ingress, interest);
		this->addEntry(interest.getName(), interest.getNonce(), sendNode, ns3::Seconds(deferTime), eventId, m_waitTableInt);
	}
}

void
DASB::doSendInterest(const shared_ptr<pit::Entry> &pitEntry,
				  const FaceEndpoint &egress, const FaceEndpoint &ingress,
				  const Interest &interest) {
	NFD_LOG_INFO("do send" << interest << " from=" << ingress << " to=" << egress);
	this->sendInterest(pitEntry, egress, interest);
	auto it = findEntry(interest.getName(), interest.getNonce(), m_waitTableInt);
	this->deleteEntry(it,m_waitTableInt);
}

void
DASB::doSendData(const shared_ptr<pit::Entry>& pitEntry,
                        const Data& data, const FaceEndpoint& egress) {
    NFD_LOG_DEBUG("do send"<<data<<"to= "<<egress);
    this->sendData(pitEntry, data, egress);
    auto it = findEntry(data.getName(),0, m_waitTableDat);
    this->deleteEntry(it,m_waitTableDat);                       
}

void DASB::afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
							const FaceEndpoint &ingress, const Data &data)
{
	NFD_LOG_DEBUG("afterReceiveData pitEntry=" << pitEntry->getName()
											   << " in=" << ingress << " data=" << data.getName());

    bool isTheConsumer=false;
    auto now = time::steady_clock::now();
    const auto& inface =  (pitEntry->getInRecords().begin()->getFace());
    auto egress = FaceEndpoint(inface,0);
    //判断是否是对应的Consumer
    for (const pit::InRecord& inRecord : pitEntry->getInRecords()) {
        if (inRecord.getExpiry()>now && inRecord.getFace().getId()==256+m_nodes.GetN()) {
            isTheConsumer = true;
            break;
        }
    }
    // 如果是Consumer端或Producer端，则直接转发给应用层，无需等待
    if (ingress.face.getId()==256+m_nodes.GetN() || isTheConsumer) {
        this->sendData(pitEntry, data, egress);
        return;
    }

	auto it = findEntry(data.getName(), 0, m_waitTableDat);
    const auto transport = ingress.face.getTransport();
	ns3::ndn::WifiNetDeviceTransportBroadcast* wifiTrans = dynamic_cast<ns3::ndn::WifiNetDeviceTransportBroadcast*>(transport);
	ns3::Ptr<ns3::Node> receiveNode = wifiTrans->GetNode();
	// 节点创建的face是从257开始依据节点序号依次递增的，据此计算face对端节点的序号
	int sendNodeId = (ingress.face.getId() - 257) + (receiveNode->GetId()+257 <= ingress.face.getId());
	ns3::Ptr<ns3::Node> sendNode = m_nodes[sendNodeId];

	if (it != m_waitTableDat.end() && shouldSuppress(it->preNode, sendNode, receiveNode)) {
		this->cancelSend(it->eventId);
		this->deleteEntry(it, m_waitTableDat);
        return;
	}
    NS_LOG_DEBUG("Wait to send Data " << data << " from=" << ingress <<"to= "<<egress);
	double deferTime = calculateDeferTime(sendNode, receiveNode);
	auto eventId = ns3::Simulator::Schedule(ns3::Seconds(deferTime), &DASB::doSendData, this, pitEntry, data, egress);
	this->addEntry(data.getName(), 0, sendNode, ns3::Seconds(deferTime), eventId, m_waitTableDat);
	this->sendData(pitEntry, data, egress);
}

void
DASB::afterReceiveLoopedInterest(const FaceEndpoint& ingress, const Interest& interest,
                             pit::Entry& pitEntry) {
	NFD_LOG_DEBUG("afterReceiveLoopedInterest Interest=" << pitEntry.getInterest()<< " in=" << ingress);
	auto it = findEntry(interest.getName(), interest.getNonce(), m_waitTableInt);
	if (it != m_waitTableInt.end()) {
		this->cancelSend(it->eventId);
		this->deleteEntry(it, m_waitTableInt);
	}
}

std::vector<DASB::m_tableEntry>::iterator
DASB::findEntry(const Name& name, uint32_t nonce, std::vector<m_tableEntry>& table) {
	return std::find_if(table.begin(), table.end(),
							[&](const m_tableEntry& entry) {
							return entry.interestName == name && entry.nonce == nonce;
							});
  }

void
DASB::addEntry(const Name &name, uint32_t nonce, ns3::Ptr<ns3::Node> preNode, ns3::Time deferTime, ns3::EventId eventId, std::vector<m_tableEntry>& table)
{
	m_tableEntry newEntry(name, nonce, preNode, deferTime, eventId);
        table.push_back(newEntry);
        NFD_LOG_DEBUG("Add WaitTable Entry: ("<<name <<", "<<nonce<<", " <<preNode<<", "<<deferTime.GetSeconds()<<", " <<eventId.GetUid()<<")");
}


void
DASB::deleteEntry(std::vector<DASB::m_tableEntry>::iterator it, std::vector<m_tableEntry>& table)
{
    if (it != table.end()) {
		NFD_LOG_DEBUG("Delete WaitTable Entry: ("<< it->interestName <<", "<< it->nonce<<", " << it->deferTime.GetSeconds()<<", " << it->eventId.GetUid()<<")");
		table.erase(it);
	}
}

double
DASB::calculateDeferTime(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode) {
	ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = receiveNode->GetObject<ns3::MobilityModel>();
	double distance =  mobility2->GetDistanceFrom(mobility1);
	double defer_time = abs((m_Rth-distance))*m_Tm/m_Rth;
	return defer_time;
}

bool
DASB::shouldSuppress(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode, ns3::Ptr<ns3::Node> otherNode) {
	ns3::Ptr<ns3::MobilityModel> mobilityA = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobilityB = receiveNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobilityC = otherNode->GetObject<ns3::MobilityModel>();
    double d_AB = mobilityA->GetDistanceFrom(mobilityB);
    double d_AC = mobilityA->GetDistanceFrom(mobilityC);
    double d_BC = mobilityB->GetDistanceFrom(mobilityC);
    double angle = std::acos( (pow(d_AB,2)+pow(d_AC,2)-pow(d_BC,2))/(2*d_AB*d_AC+0.0001) );
    return (angle<m_Angle);
}

void
DASB::cancelSend(ns3::EventId eventId) {
	ns3::Simulator::Cancel(eventId);
	NS_LOG_DEBUG("Cancel EventId="<<eventId.GetUid());
}

} // namespace fw
} // namespace nfd
