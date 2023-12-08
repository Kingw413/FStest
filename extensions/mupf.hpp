#ifndef NFD_DAEMON_FW_MUPF_HPP
#define NFD_DAEMON_FW_MUPF_HPP

#include "ns3/ndnSIM/NFD/daemon/fw/strategy.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/process-nack-traits.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/retx-suppression-exponential.hpp"
#include "ns3/node-container.h"
#include "ns3/node.h"
#include "ns3/vector.h"
namespace nfd
{
namespace fw
{

class MUPF : public Strategy, public ProcessNackTraits<MUPF>
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

public:
	explicit MUPF(Forwarder &forwarder, const Name &name = getStrategyName());

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
    contentDiscovery(const FaceEndpoint& ingress, const Interest& interest, const shared_ptr<pit::Entry> &pitEntry);

    /*建立路径，在内容发现完成后触发*/
    void
    unicastPathBuilding(const ndn::Name prefix, ns3::Ptr<ns3::Node> srcNode, ns3::Ptr<ns3::Node> providerNode);

    /*在FIB中选择下一跳*/
    nfd::fib::NextHop
    selectFIB(const fib::NextHopList& nexthops);

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

    /*判断是否在通信范围*/
    bool
    isInRegion(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> recvNode);

    /* 得到当前node
     * 通过判断this指针与哪个node的strategy指向相同对象，来判别当前的node
    */
    ns3::Ptr<ns3::Node>
    getNode(MUPF& local_strategy);


private:
	static const double Rth;
    static const double Mu;
    static const double Phi;
    static const double Omega;
    static const double Alpha;
    static const double Beta;

	ns3::NodeContainer m_nodes;
    bool m_isContentDiscovery = false;
	std::vector<MUPF::weightTableEntry> m_WT;
	std::vector<MUPF::neighborTableEntry> m_NT;

	PUBLIC_WITH_TESTS_ELSE_PRIVATE : static const time::milliseconds RETX_SUPPRESSION_INITIAL;
	static const time::milliseconds RETX_SUPPRESSION_MAX;
	RetxSuppressionExponential m_retxSuppression;

	friend ProcessNackTraits<MUPF>;

	// Forwarder &fw;
};

} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_MUPF_HPP