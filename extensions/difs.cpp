#include "difs.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/algorithm.hpp"
#include "common/logger.hpp"
#include "ndn-wifi-net-device-transport-broadcast.hpp"
#include "ns3/mobility-model.h"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/apps/ndn-producer.hpp"
#include "ns3/simulator.h"

namespace nfd{
namespace fw{

NFD_REGISTER_STRATEGY(DIFS);
NFD_LOG_INIT(DIFS);

const time::milliseconds DIFS::RETX_SUPPRESSION_INITIAL(10);
const time::milliseconds DIFS::RETX_SUPPRESSION_MAX(250);

DIFS::DIFS(Forwarder &forwarder, const Name &name)
	: Strategy(forwarder), ProcessNackTraits(this),
	  m_retxSuppression(RETX_SUPPRESSION_INITIAL, RetxSuppressionExponential::DEFAULT_MULTIPLIER,
						RETX_SUPPRESSION_MAX),
	  m_forwarder(forwarder),
	  m_nodes(ns3::NodeContainer::GetGlobal()), m_Rth(200)
{
	ParsedInstanceName parsed = parseInstanceName(name);
	if (!parsed.parameters.empty())
	{
		NDN_THROW(std::invalid_argument("DIFS does not accept parameters"));
	}
	if (parsed.version && *parsed.version != getStrategyName()[-1].toVersion())
	{
		NDN_THROW(std::invalid_argument(
			"DIFS does not support version " + to_string(*parsed.version)));
	}
	this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

const Name &
DIFS::getStrategyName()
{
	static Name strategyName("/localhost/nfd/strategy/DIFS/%FD%01");
	return strategyName;
}

void DIFS::afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
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

    this->updateNeighborList(receiveNode);
    std::vector<DecisionEntry> decisionList = this->calculateDecisionList(sendNode,receiveNode);
    this->customNormalize(decisionList);

    DecisionEntry idealSolution = this->calculateIdealSolution(decisionList);
    DecisionEntry negIdealSolution = this->calculateNegativeIdealSolution(decisionList);
    DecisionEntry optimalDecision = this->getOptimalDecision(decisionList, idealSolution, negIdealSolution);

    if (optimalDecision.node->GetId() == receiveNode->GetId()) {
        NFD_LOG_INFO("do Send Interest" << interest << " from=" << ingress << " to=" << egress);
        this->sendInterest(pitEntry, egress, interest);
    }
    else {
        this->setExpiryTimer(pitEntry, 0_ms);
    }
}

void
DIFS::afterContentStoreHit(const shared_ptr<pit::Entry> &pitEntry,
                                const FaceEndpoint &ingress, const Data &data)
{
    NFD_LOG_DEBUG("afterContentStoreHit pitEntry=" << pitEntry->getName()
                    << " in=" << ingress << " data=" << data.getName());

    this->sendData(pitEntry, data, ingress);
    NFD_LOG_DEBUG("do Send Data=" << data.getName() << ", from=" << ingress);
}

void
DIFS::afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
							const FaceEndpoint &ingress, const Data &data)
{
	Interest interest = pitEntry->getInterest();
	const auto& inface =  (pitEntry->getInRecords().begin()->getFace());
    auto egress = FaceEndpoint(inface,0);
	this->sendData(pitEntry,data,egress);
    NFD_LOG_DEBUG("do Send Data="<<data.getName()<<", from="<<ingress<<", to="<<egress);
}

double
DIFS::calculateDistance(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode) {
	ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = receiveNode->GetObject<ns3::MobilityModel>();
	double distance =  mobility2->GetDistanceFrom(mobility1);
	return distance;
}

double
DIFS::calculateRelativeVel(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = receiveNode->GetObject<ns3::MobilityModel>();
    double relative_x = mobility1->GetVelocity().x - mobility2->GetVelocity().x;
    double relative_y = mobility1->GetVelocity().y - mobility2->GetVelocity().y;
    double relativeVel = sqrt( pow(relative_x, 2) + pow(relative_y, 2) );
    return relativeVel;
}

double
DIFS::calculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode) {
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

std::vector<DIFS::DecisionEntry>
DIFS::calculateDecisionList(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode) {
    ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
	ns3::Ptr<ns3::MobilityModel> mobility2 = receiveNode->GetObject<ns3::MobilityModel>(); 
    double x_c = mobility1->GetPosition().x;
    double y_c = mobility1->GetPosition().y;
    double x_r = mobility2->GetPosition().x;
    double y_r = mobility2->GetPosition().y;
    std::vector<DecisionEntry> decisionList;
    for (auto& neighEntry: m_NeighborList) {
        double distance = this->calculateDistance(sendNode, neighEntry.node);
        bool shouldInDL = false;
        if ( (x_r>x_c && neighEntry.x>x_c && distance<m_Rth)  || ( x_r<x_c && neighEntry.x<x_c && distance<m_Rth) ){
            shouldInDL = true;
        }
        if (shouldInDL) {
            double relativeVel = this->calculateRelativeVel(sendNode, neighEntry.node);
            double let = this->calculateLET(sendNode, neighEntry.node);
            DIFS::DecisionEntry decisionEntry(neighEntry.node, distance, relativeVel, let);
            decisionList.push_back(decisionEntry);
            NFD_LOG_DEBUG(neighEntry.node->GetId()<<", "<<distance<<", "<<relativeVel<<", "<<let);
        }
    }
    return decisionList;
}

std::vector<DIFS::DecisionEntry>&
DIFS::customNormalize(std::vector<DIFS::DecisionEntry>& decisionList) {
    double distanceSum=0, velSum=0, letSum=0;
    for (const auto& decisionEntry: decisionList) {
        distanceSum += pow(decisionEntry.Distance, 2);
        velSum += pow(decisionEntry.RelativeVel, 2);
        letSum += pow(decisionEntry.LET, 2);
    }
    for (auto& decisionEntry : decisionList) {
        decisionEntry.Distance = 1.0/3.0 * decisionEntry.Distance / sqrt(distanceSum+0.001);
        decisionEntry.RelativeVel = 1.0/3.0 * decisionEntry.RelativeVel / sqrt(velSum+0.001);
        decisionEntry.LET = 1.0/3.0 * decisionEntry.LET / sqrt(letSum+0.001);
        // NFD_LOG_DEBUG("Node="<<decisionEntry.node->GetId()<<", D="<<decisionEntry.Distance<<", S="<<decisionEntry.RelativeVel<<", L="<<decisionEntry.LET);
    }
    return decisionList;
}

DIFS::DecisionEntry
DIFS::calculateIdealSolution(std::vector<DIFS::DecisionEntry>& decisionList) {
    DecisionEntry idealSolution(
        decisionList[0].node,  // 使用第一个节点作为默认值
        ( std::max_element(decisionList.begin(), decisionList.end(), [](const auto& a, const auto& b) { return a.Distance < b.Distance; }) )->Distance,
        ( std::min_element(decisionList.begin(), decisionList.end(), [](const auto& a, const auto& b) { return a.RelativeVel < b.RelativeVel; }) )->RelativeVel,
        ( std::max_element(decisionList.begin(), decisionList.end(), [](const auto& a, const auto& b) { return a.LET < b.LET; }) )->LET
    );
    // NFD_LOG_DEBUG("Ideal: D="<<idealSolution.Distance<<", S="<<idealSolution.RelativeVel<<", L="<<idealSolution.LET);
    return idealSolution;
}

DIFS::DecisionEntry
DIFS::calculateNegativeIdealSolution(std::vector<DIFS::DecisionEntry>& decisionList) {
    DecisionEntry negativeIdealSolution(
        decisionList[0].node,  // 使用第一个节点作为默认值
        std::min_element(decisionList.begin(), decisionList.end(), [](const auto& a, const auto& b) { return a.Distance < b.Distance; })->Distance,
        std::max_element(decisionList.begin(), decisionList.end(), [](const auto& a, const auto& b) { return a.RelativeVel < b.RelativeVel; })->RelativeVel,
        std::min_element(decisionList.begin(), decisionList.end(), [](const auto& a, const auto& b) { return a.LET < b.LET; })->LET
    );
    // NFD_LOG_DEBUG("Neg: D="<<negativeIdealSolution.Distance<<", S="<<negativeIdealSolution.RelativeVel<<", L="<<negativeIdealSolution.LET);

    return negativeIdealSolution;
}

double
DIFS::calculateCloseness(const DIFS::DecisionEntry& entry, const DIFS::DecisionEntry& idealSolution, const DIFS::DecisionEntry& negativeIdealSolution) {
    double distanceIdealDeviation = entry.Distance - idealSolution.Distance;
    double relativeVelIdealDeviation = entry.RelativeVel - idealSolution.RelativeVel;
    double LETIdealDeviation = entry.LET - idealSolution.LET;
    double closenessToIdeal = sqrt( pow(distanceIdealDeviation, 2) + pow(relativeVelIdealDeviation, 2) +pow(LETIdealDeviation, 2) );
    // NFD_LOG_DEBUG("disToIdeal="<<distanceIdealDeviation<<", velToIdeal="<<relativeVelIdealDeviation<<", letIdeal="<<LETIdealDeviation<<" clossToIdeal="<<closenessToIdeal);

    double distanceNegDeviation = entry.Distance - negativeIdealSolution.Distance;
    double relativeVelNegDeviation = entry.RelativeVel - negativeIdealSolution.RelativeVel;
    double LETNegDeviation = entry.LET - negativeIdealSolution.LET;
    double closenessToNeg = sqrt( pow(distanceNegDeviation, 2) + pow(relativeVelNegDeviation, 2) +pow(LETNegDeviation, 2) );
    // NFD_LOG_DEBUG("disToNeg="<<distanceNegDeviation<<", velToIdeal="<<relativeVelNegDeviation<<", letIdeal="<<LETNegDeviation<<" clossToIdeal="<<closenessToNeg);

    double closeness = closenessToNeg / (closenessToIdeal + closenessToNeg);
    // NFD_LOG_DEBUG("node="<<entry.node->GetId()<<", closeness="<<closeness);

    return closeness;
}

DIFS::DecisionEntry
DIFS::getOptimalDecision(std::vector<DIFS::DecisionEntry>& decisionList, const DIFS::DecisionEntry& idealSolution, const DIFS::DecisionEntry& negativeIdealSolution) {
    std::vector<double> closenessValues;
    for (const auto& entry : decisionList) {
        double closeness = this->calculateCloseness(entry, idealSolution, negativeIdealSolution);
        closenessValues.push_back(closeness);
    }
    size_t optIndex = std::distance(closenessValues.begin(), std::max_element(closenessValues.begin(), closenessValues.end()));
    DecisionEntry optimalDecision = decisionList[optIndex];
    // NFD_LOG_DEBUG("Optimal Decision = "<<optimalDecision.node->GetId());
    return optimalDecision;
}

void
DIFS::updateNeighborList(ns3::Ptr<ns3::Node> localNode) {
    for (auto& node : m_nodes) {
        double distance = calculateDistance(node, localNode);
        auto it  = std::find_if(m_NeighborList.begin(), m_NeighborList.end(),
                        [&](const NeighborEntry& entry) { return entry.node == node; });
        if (distance>m_Rth) { 
            if (it!=m_NeighborList.end()) {
                m_NeighborList.erase(it);
            }
            continue;
        }

	    ns3::Ptr<ns3::MobilityModel> mobility = node->GetObject<ns3::MobilityModel>(); 
        double x = mobility->GetPosition().x;
        double y = mobility->GetPosition().y;
        double v_x = mobility->GetVelocity().x;
        double v_y = mobility->GetVelocity().y;
        DIFS::NeighborEntry entry(node, x, y, v_x, v_y);

        if (it != m_NeighborList.end()) {
            it->x = x;
            it->y = y;
            it->v_x = v_x;
            it->v_y = v_y;
        }
        else
            m_NeighborList.push_back(entry);
    }
}

} // namespace fw
} // namespace nfd
