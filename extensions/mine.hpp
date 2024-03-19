#ifndef NFD_DAEMON_FW_MINE_HPP
#define NFD_DAEMON_FW_MINE_HPP

#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/strategy.hpp"
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
  double distance;
  double sisr;
  double srtt;
  FaceStats(Face* f, double d, double isr, double rtt) : face(f), distance(d), sisr(isr), srtt(rtt) {}
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

    bool
    isProducer(ns3::Ptr<ns3::Node> node);

    /*To get the content Souces*/
    std::set<ns3::Ptr<ns3::Node>>
    getContentSources(const Interest &interest);

    /*To get candidate relays for each content source*/
    std::set<Face*>
    getCandidateForwarders(const fib::NextHopList &nexthops, ns3::Ptr<ns3::Node> curNode, std::set<ns3::Ptr<ns3::Node>> srcNodes);

    /*在FIB中选择下一跳*/
    Face*
    selectFIB(ns3::Ptr<ns3::Node> localNode, const Interest &interest, std::set<Face *> candidateForwarders, const fib::Entry &fibEntry);

    /*计算节点间距离*/				
    double
    calculateDistance(ns3::Ptr<ns3::Node> node1, ns3::Ptr<ns3::Node> node2);

	/*计算LET*/
	double
	calculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode);

    /*归一化*/
    std::vector<FaceStats>
    customNormalize(std::vector<FaceStats>& faceList);

    /*计算正理想解*/
    FaceStats
    calculateIdealSolution(std::vector<FaceStats>& faceList);

    /*计算负理想解*/
    FaceStats
    calculateNegativeIdealSolution(std::vector<FaceStats> &faceList);

    /*计算最终的权重*/
    double
    calculateCloseness(const FaceStats& entry, const FaceStats& idealSolution, const FaceStats& negativeIdealSolution);

    /*得到最优解*/
    FaceStats &
    getOptimalDecision(std::vector<FaceStats> &faceList);

    /* 得到当前node
     * 通过判断this指针与哪个node的strategy指向相同对象，来判别当前的node
    */
    ns3::Ptr<ns3::Node>
    getNode(MINE& local_strategy);


private:
	static const double Rth;

	ns3::NodeContainer m_nodes;
	std::vector<MINE::neighborTableEntry> m_NT;
    MineMeasurements m_measurements;

	PUBLIC_WITH_TESTS_ELSE_PRIVATE : static const time::milliseconds RETX_SUPPRESSION_INITIAL;
	static const time::milliseconds RETX_SUPPRESSION_MAX;
	RetxSuppressionExponential m_retxSuppression;
};

} // namespace mine
} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_MINE_HPP