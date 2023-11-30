#ifndef NFD_DAEMON_FW_LSIF_HPP
#define NFD_DAEMON_FW_LSIF_HPP

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
class LSIF : public Strategy, public ProcessNackTraits<LSIF>
{
public:
	explicit LSIF(Forwarder &forwarder, const Name &name = getStrategyName());

	static const Name &
	getStrategyName();

	void
	afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
					const shared_ptr<pit::Entry> &pitEntry) override;
	void
	afterReceiveData(const shared_ptr<pit::Entry> &pitEntry,
					const FaceEndpoint &ingress, const Data &data) override;
					
	/*判断是否在通信范围内*/
	bool
	isInRegion(const FaceEndpoint &ingress);

    /*计算LET*/
    double
    caculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode);

private:
	friend ProcessNackTraits<LSIF>;
	RetxSuppressionExponential m_retxSuppression;

	PUBLIC_WITH_TESTS_ELSE_PRIVATE : static const time::milliseconds RETX_SUPPRESSION_INITIAL;
	static const time::milliseconds RETX_SUPPRESSION_MAX;

private:
	Forwarder &m_forwarder;
	ns3::NodeContainer m_nodes;
	double m_Rth;
    double m_LET_alpha;
};

} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_MULTICAST_STRATEGY_HPP
