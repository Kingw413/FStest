#ifndef NFD_DAEMON_FW_PRFS_HPP
#define NFD_DAEMON_FW_PRFS_HPP

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

class PRFS : public Strategy, public ProcessNackTraits<PRFS>
{
struct  inteAddField {
	const Name name;
	uint32_t nonce;
	ns3::Ptr<ns3::Node> FIRD;
	ns3::Ptr<ns3::Node> FIRRD;
	inteAddField(const Name na, uint32_t n, ns3::Ptr<ns3::Node>fird, ns3::Ptr<ns3::Node> firrd) : name(na), nonce(n), FIRD(fird), FIRRD(firrd) {}
};

public:
	explicit PRFS(Forwarder &forwarder, const Name &name = getStrategyName());

	static const Name &
	getStrategyName();

	void
	afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
						 const shared_ptr<pit::Entry> &pitEntry) override;
	void
	afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
					 const FaceEndpoint &ingress, const Data &data) override;

	/*避免在Interest包中添加字段
	* 而是在每个节点处添加一个Table，其表项对应各个Interest的FIRD和FIRRD
	* 实现与添加字段相同的功能*/
	void
	setNextHop(const fib::NextHopList &nexthops,
			   const Interest &interest,
			   const shared_ptr<pit::Entry> &pitEntry,
			   bool isRD,
			   bool isConsumer);

	/*判断是否沿路方向*/
	bool
	isRoadDirection(ns3::Ptr<ns3::Node> node, ns3::Ptr<ns3::Node> remote_node);

	/*计算沿路距离*/
	double
	caculateDR(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode);

    /*计算节点间距离*/				
    double
    calculateDistance(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode);

	/*计算LET，因为原文中未提及LET的阈值具体设置为多少，因此此函数实际未调用*/
	double
	calculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode);

	// std::map<uint32_t, std::vector<int>> &
	// getHOP()
	// {
	// 	return m_hop;
	// }

	// void
	// setHopList(uint32_t nonce, std::map<uint32_t, std::vector<int>> &, std::map<uint32_t, std::vector<int>> &hop, int hopId);

	// void
	// updateHopList(int preId, int curId, const Interest &interest);

	// void
	// getHopCounts(const Interest &interest,
	// 			 ns3::Ptr<ns3::Node> node);

private:
	double m_Rth;
	double m_LET_alpha;
	ns3::NodeContainer m_nodes;
	std::vector<PRFS::inteAddField> m_IntTable;
	// std::map<uint32_t, std::vector<int>> m_hop;

	PUBLIC_WITH_TESTS_ELSE_PRIVATE : static const time::milliseconds RETX_SUPPRESSION_INITIAL;
	static const time::milliseconds RETX_SUPPRESSION_MAX;
	RetxSuppressionExponential m_retxSuppression;

	friend ProcessNackTraits<PRFS>;

	// Forwarder &fw;
};

} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_PRFS_HPP