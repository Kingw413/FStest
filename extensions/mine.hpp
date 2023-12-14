#ifndef NFD_DAEMON_FW_MINE_HPP
#define NFD_DAEMON_FW_MINE_HPP

#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/strategy.hpp"
// #include "ns3/ndnSIM/NFD/daemon/fw/process-nack-traits.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/retx-suppression-exponential.hpp"
#include "mine-measurements.hpp"
#include "ns3/node-container.h"
#include "ns3/node.h"
#include "ns3/vector.h"
namespace nfd {
namespace fw {
namespace mine {

class MINE : public Strategy
{
struct  weightTableEntry {
	ns3::Ptr<ns3::Node> node;
    double Dis;
    double Dir;
    double TD;
    double Score;
	weightTableEntry(ns3::Ptr<ns3::Node> n, double distance, double direction, double density, double score) : node(n), Dis(distance), Dir(direction), TD(density), Score(score) {}
};

struct  neighborTableEntry {
	ns3::Ptr<ns3::Node> node;
    ns3::Vector3D position;
    ns3::Vector3D velocity;
    double LET;
    double linkProb;
	neighborTableEntry(ns3::Ptr<ns3::Node> n, ns3::Vector3D pos, ns3::Vector3D vel, double let, double prob) : node(n), position(pos), velocity(vel), LET(let), linkProb(prob) {}
};

struct FaceStats
{
  Face* face;
  double let;
  double lap;
  double srtt;
};

public:
	explicit MINE(Forwarder &forwarder, const Name &name = getStrategyName());

	static const Name &
	getStrategyName();

	void
	afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
						 const shared_ptr<pit::Entry> &pitEntry) override;
	
    void
    beforeSatisfyInterest(const shared_ptr<pit::Entry>& pitEntry,
                        const FaceEndpoint& ingress, const Data& data) override;
    void
    afterContentStoreHit(const shared_ptr<pit::Entry>& pitEntry,
                                const FaceEndpoint& ingress, const Data& data) override;
    void
	afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
					 const FaceEndpoint &ingress, const Data &data) override;

    /*内容发现，当FIB表中没有匹配项时触发*/
    void
    contentDiscovery(ns3::Ptr<ns3::Node> localNode, const FaceEndpoint& ingress, const fib::NextHopList& nexthops, const Interest& interest, const shared_ptr<pit::Entry> &pitEntry);

    /*收到Discovery对应的Data包后触发*/
    void
    afterReceiveDiscoveryData(const shared_ptr<pit::Entry>& pitEntry,
                        const FaceEndpoint& ingress, const Data& data);

    /*添加CPT，在内容发现完成后触发*/
    void
    createCPT(ns3::Ptr<ns3::Node> providerNode);

    /*建立路径，在内容发现完成后触发*/
    void
    unicastPathBuilding(const ndn::Name prefix, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> providerNode);

    /*在FIB中选择下一跳*/
    Face*
    selectFIB(ns3::Ptr<ns3::Node> localNode, const Interest& interest, const fib::NextHopList& nexthops, const fib::Entry& fibEntry);

    /*更新当前节点的FIB，周期性触发*/
    void
    updateFIB(const ndn::Name prefix, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> providerNode, const fib::NextHopList& nexthops);

    /*判断是否在相交区域*/
    bool
    isIntermediateNode(ns3::Ptr<ns3::Node> node, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> desNode);

    /*计算节点间距离*/				
    double
    calculateDistance(ns3::Ptr<ns3::Node> localNode, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> desNode);

    /*计算密度*/
    double
    calculateDensity(ns3::Ptr<ns3::Node> node);

    /*计算方向相似度*/
    double
    calculateDirection(ns3::Ptr<ns3::Node> node, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> desNode);

	/*计算LET*/
	double
	calculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode);

    /*计算链路可用概率*/
    double
    calculateLAP(double t, double delta_t);


    /*归一化DL*/
    std::vector<FaceStats>
    customNormalize(std::vector<FaceStats>& decisionList);

    /*计算正理想解*/
    FaceStats&
    calculateIdealSolution(std::vector<FaceStats>& decisionList);

    /*计算负理想解*/
    FaceStats&
    calculateNegativeIdealSolution(std::vector<FaceStats>& decisionList);

    /*计算最终的权重*/
    double
    calculateCloseness(const FaceStats& entry, const FaceStats& idealSolution, const FaceStats& negativeIdealSolution);

    /*得到最优解*/
    FaceStats&
    getOptimalDecision(std::vector<FaceStats>& decisionList, const FaceStats& idealSolution, const FaceStats& negativeIdealSolution);


    /*判断是否在通信范围*/
    bool
    isInRegion(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> recvNode);

    bool
    isNextHopEligible(const Face& inFace,
                  const fib::NextHop& nexthop,
                  ns3::Ptr<ns3::Node> node);

    /* 得到当前node
     * 通过判断this指针与哪个node的strategy指向相同对象，来判别当前的node
    */
    ns3::Ptr<ns3::Node>
    getNode(MINE& local_strategy);


private:
	static const double Rth;
    static const double Mu;
    static const double Phi;
    static const double Omega;
    static const double Alpha;
    static const double Beta;

	ns3::NodeContainer m_nodes;
	// std::vector<MINE::weightTableEntry> m_WT;
	std::vector<MINE::neighborTableEntry> m_NT;
    std::vector<ns3::Ptr<ns3::Node>> m_CPT;

    MineMeasurements m_measurements;

	PUBLIC_WITH_TESTS_ELSE_PRIVATE : static const time::milliseconds RETX_SUPPRESSION_INITIAL;
	static const time::milliseconds RETX_SUPPRESSION_MAX;
	RetxSuppressionExponential m_retxSuppression;

};

} // namespace mine
} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_MINE_HPP