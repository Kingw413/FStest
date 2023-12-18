#ifndef NFD_DAEMON_FW_DASB_HPP
#define NFD_DAEMON_FW_FLOD_HPP

#include "ns3/ndnSIM/NFD/daemon/fw/strategy.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/process-nack-traits.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/retx-suppression-exponential.hpp"
#include "ns3/node-container.h"
#include "ns3/node.h"
#include "ns3/ndnSIM/ndn-cxx/name.hpp"
#include <vector>
#include <map>

namespace nfd{
namespace fw{
class DASB : public Strategy, public ProcessNackTraits<DASB>
{
struct m_tableEntry {
	Name interestName;
	uint32_t nonce;
    ns3::Ptr<ns3::Node> preNode;
    ns3::Time deferTime;
	ns3::EventId eventId;
	m_tableEntry(const Name &name, uint32_t n, ns3::Ptr<ns3::Node> node, ns3::Time t, ns3::EventId id) : interestName(name), nonce(n), preNode(node), deferTime(t), eventId(id) {}
};

public:
	explicit DASB(Forwarder &forwarder, const Name &name = getStrategyName());

	static const Name &
	getStrategyName();

	void
	afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
					const shared_ptr<pit::Entry> &pitEntry) override;

	/*因为Data包也是广播的，因此在此函数内部应调用sendData而不是sendDataToAll，只需要发送一次Data即可*/
    void
	afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
					const FaceEndpoint &ingress, const Data &data) override;
  	void
	afterReceiveLoopedInterest(const FaceEndpoint& ingress, const Interest& interest,
                    pit::Entry& pitEntry) override;

	/*判断是否在通信范围内*/
	bool
	isInRegion(nfd::fib::NextHop hop);

	/*执行发送Interest*/
	void
    doSendInterest(const shared_ptr<pit::Entry> &pitEntry,
				const FaceEndpoint &egress, const FaceEndpoint &ingress,
				const Interest &interest);

    /*执行发送Data*/
    void
    doSendData(const shared_ptr<pit::Entry>& pitEntry,
                        const Data& data, const FaceEndpoint& egress);

	/*查找WaitTable中是否已有相同的<Interest, Nonce>*/
	std::vector<DASB::m_tableEntry>::iterator
	findEntry(const Name &targetName, uint32_t targetNonce, std::vector<m_tableEntry>& table);

	/*在WaitTable中添加新表项
	* 当findEntry判断为否时触发
	*/
	void
	addEntry(const Name &name, uint32_t nonce, ns3::Ptr<ns3::Node> preNode, ns3::Time deferTime, ns3::EventId eventId, std::vector<m_tableEntry>& table);

	/**删除WaitTable中的某表项
	*   当findEntry判断为真或收到相应Data包时触发
	*/
	// void
	// deleteEntry(const Name &targetName, uint32_t targetNonce);
	void
	deleteEntry(std::vector<DASB::m_tableEntry>::iterator it, std::vector<m_tableEntry>& table);

	/* 计算等待转发的延迟时间*/
	double
	calculateDeferTime(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode);

	/*计算角度*/
    bool
    shouldSuppress(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode, ns3::Ptr<ns3::Node> otherNode);

    /*取消转发*/
	void
	cancelSend(ns3::EventId eventId);

	// std::map<uint32_t, std::vector<int>> &
	// getHOP()
	// {
	// 	return m_hop;
	// }

	// void
	// setHopList(uint32_t nonce, std::map<uint32_t, std::vector<int>> &, std::map<uint32_t, std::vector<int>> &hop, int hopId, int next_hopId);

	// void
	// updateHopList(nfd::face::Face &inface, nfd::face::Face &outface, const Interest &interest);

	// void
	// getHopCounts(const Interest &interest,
	// 			 ns3::Ptr<ns3::Node> node);

private:
	friend ProcessNackTraits<DASB>;
	RetxSuppressionExponential m_retxSuppression;

	PUBLIC_WITH_TESTS_ELSE_PRIVATE : static const time::milliseconds RETX_SUPPRESSION_INITIAL;
	static const time::milliseconds RETX_SUPPRESSION_MAX;

    static const double DEFER_TIME_MAX;
    static const double TRANSMISSION_RANGE;
    static const double SUPPRESSION_ANGLE;
private:
    double m_Tm;
	double m_Rth;
    double m_Angle;
	Forwarder &m_forwarder;
	ns3::NodeContainer m_nodes;
	// std::map<uint32_t, std::vector<int>> m_hop;
	std::vector<m_tableEntry> m_waitTableInt;
	std::vector<m_tableEntry> m_waitTableDat;

};

} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_MULTICAST_STRATEGY_HPP
