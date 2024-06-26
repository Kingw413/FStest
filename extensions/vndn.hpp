#ifndef NFD_DAEMON_FW_VNDN_HPP
#define NFD_DAEMON_FW_VNDN_HPP

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
class VNDN : public Strategy
{
struct m_tableEntry {
	Name interestName;
	uint32_t nonce;
	ns3::Time deferTime;
	ns3::EventId eventId;
	m_tableEntry(const Name &name, uint32_t n, ns3::Time t, ns3::EventId id) : interestName(name), nonce(n), deferTime(t), eventId(id) {}
};

public:
	explicit VNDN(Forwarder &forwarder, const Name &name = getStrategyName());

	static const Name &
	getStrategyName();

	void
	afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
					const shared_ptr<pit::Entry> &pitEntry) override;

	void afterContentStoreHit(const shared_ptr<pit::Entry> &pitEntry,
									const FaceEndpoint &ingress, const Data &data) override;
	
	void afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
									const FaceEndpoint &ingress, const Data &data) override;

	/*收到相同的广播包时触发*/
	void
	afterReceiveLoopedInterest(const FaceEndpoint& ingress, const Interest& interest,
                    pit::Entry& pitEntry) override;

	/*执行发送*/
	void doSend(const shared_ptr<pit::Entry> &pitEntry,
				const FaceEndpoint &egress, const FaceEndpoint &ingress,
				const Interest &interest);

	/*查找WaitTable中是否已有相同的<Interest, Nonce>*/
	std::vector<VNDN::m_tableEntry>::iterator
	findEntry(const Name &targetName, uint32_t targetNonce);

	/*在WaitTable中添加新表项
	* 当findEntry判断为否时触发
	*/
	void
	addEntry(const Name &interestName, uint32_t nonce, ns3::Time deferTime, ns3::EventId eventId);

	/**删除WaitTable中的某表项
	*   当findEntry判断为真或收到相应Data包时触发
	*/
	void
	deleteEntry(std::vector<VNDN::m_tableEntry>::iterator it);

	/* 计算等待转发的延迟时间*/
	double
	caculateDeferTime(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode);

	/*取消转发*/
	void
	cancelSend(Interest interest, ns3::EventId eventId);

private:
	RetxSuppressionExponential m_retxSuppression;

	PUBLIC_WITH_TESTS_ELSE_PRIVATE : static const time::milliseconds RETX_SUPPRESSION_INITIAL;
	static const time::milliseconds RETX_SUPPRESSION_MAX;

private:
	ns3::NodeContainer m_nodes;
	double m_Rth;
	// std::map<uint32_t, std::vector<int>> m_hop;
	std::vector<m_tableEntry> m_waitTable;

};

} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_MULTICAST_STRATEGY_HPP
