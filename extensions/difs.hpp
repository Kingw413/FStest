#ifndef NFD_DAEMON_FW_DIFS_HPP
#define NFD_DAEMON_FW_DIFS_HPP

#include "ns3/ndnSIM/NFD/daemon/fw/strategy.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/process-nack-traits.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/retx-suppression-exponential.hpp"
#include "ns3/node-container.h"
#include "ns3/node.h"
#include "ns3/ndnSIM/ndn-cxx/name.hpp"
#include <vector>

namespace nfd{
namespace fw{
class DIFS : public Strategy, public ProcessNackTraits<DIFS>
{
struct NeighborEntry {
	ns3::Ptr<ns3::Node> node;
    double x;
    double y;
    double v_x;
    double v_y;
	NeighborEntry( ns3::Ptr<ns3::Node> &i, double x_coor, double y_coor, double vel_x, double vel_y) : node(i), x(x_coor), y(y_coor), v_x(vel_x), v_y(vel_y) {}
};

struct DecisionEntry {
	ns3::Ptr<ns3::Node> node;
    double Distance;
    double RelativeVel;
    double LET;
	DecisionEntry( ns3::Ptr<ns3::Node> &i, double D, double S, double L) : node(i), Distance(D), RelativeVel(S), LET(L) {}
};

public:
	explicit DIFS(Forwarder &forwarder, const Name &name = getStrategyName());

	static const Name &
	getStrategyName();

	void
	afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
					const shared_ptr<pit::Entry> &pitEntry) override;
	void
	afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
					const FaceEndpoint &ingress, const Data &data) override;

    /*计算节点间距离*/				
    double
    calculateDistance(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode);

    /*计算相对速度*/
    double
    calculateRelativeVel(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode);

    /*计算LET*/
    double
    calculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode);

    /*计算DL*/
    std::vector<DecisionEntry>
    calculateDecisionList(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receivenode);

    /*归一化DL*/
    std::vector<DecisionEntry>&
    customNormalize(std::vector<DIFS::DecisionEntry>& decisionList);

    /*计算正理想解*/
    DIFS::DecisionEntry
    calculateIdealSolution(std::vector<DIFS::DecisionEntry>& decisionList);

    /*计算负理想解*/
    DIFS::DecisionEntry
    calculateNegativeIdealSolution(std::vector<DIFS::DecisionEntry>& decisionList);

    /*计算最终的权重*/
    double
    calculateCloseness(const DIFS::DecisionEntry& entry, const DIFS::DecisionEntry& idealSolution, const DIFS::DecisionEntry& negativeIdealSolution);

    /*得到最优解*/
    DecisionEntry
    getOptimalDecision(std::vector<DIFS::DecisionEntry>& decisionList, const DIFS::DecisionEntry& idealSolution, const DIFS::DecisionEntry& negativeIdealSolution);

    /*更新NL
    * 理论上应当定期发送beacon，节点收到后再更新NL，此处暂时设定为实时更新，即每次转发兴趣包时更新
    */
    void
    updateNeighborList(ns3::Ptr<ns3::Node> localNode);

private:
	friend ProcessNackTraits<DIFS>;
	RetxSuppressionExponential m_retxSuppression;

	PUBLIC_WITH_TESTS_ELSE_PRIVATE : static const time::milliseconds RETX_SUPPRESSION_INITIAL;
	static const time::milliseconds RETX_SUPPRESSION_MAX;

private:
	Forwarder &m_forwarder;
	ns3::NodeContainer m_nodes;
	double m_Rth;
    std::vector<NeighborEntry> m_NeighborList;
    // std::vector<DecisionEntry> m_DecisionList;
};

} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_DIFS_STRATEGY_HPP
