#ifndef NFD_DAEMON_FW_MINE2_MEASUREMENTS_HPP
#define NFD_DAEMON_FW_MINE2_MEASUREMENTS_HPP

#include "fw/strategy-info.hpp"
#include "fw/forwarder-counters.hpp"
#include "table/measurements-accessor.hpp"

#include <ndn-cxx/util/rtt-estimator.hpp>

namespace nfd {
namespace fw {
namespace mine2 {
/** \brief Strategy information for each face in a namespace
*/
class FaceInfo
{
public:
  explicit
  FaceInfo(shared_ptr<const ndn::util::RttEstimator::Options> opts)
    : m_rttEstimator(std::move(opts))
  {
  }
  void
  recordRtt(time::nanoseconds rtt)
  {
    m_lastRtt = rtt;
    m_rttEstimator.addMeasurement(rtt);
  }

  time::nanoseconds
  getLastRtt() const
  {
    return m_lastRtt;
  }

  time::nanoseconds
  getSrtt() const
  {
    return m_rttEstimator.getSmoothedRtt();
  }

  void
  recordISR(double newISR)
  {
    m_lastISR = newISR;
    if (m_isr == -1.0)
    {
      m_isr = newISR;
    }
    else
    {
      m_isr = 0.5 * newISR + (1 - 0.5) * m_isr;
    }  
}

  double
  getLastISR() const
  {
    return m_lastISR;
  }
  double getSmoothedISR() const
  {
    return m_isr;
  }

public:
  static const time::nanoseconds RTT_NO_MEASUREMENT;
  static const time::nanoseconds RTT_TIMEOUT;
  ForwarderCounters m_counters;

private:
  ndn::util::RttEstimator m_rttEstimator;
  time::nanoseconds m_lastRtt = RTT_NO_MEASUREMENT;
  double m_isr=-1;
  double m_lastISR=0.0;

  // Timeout associated with measurement
  scheduler::ScopedEventId m_measurementExpiration;
  friend class NamespaceInfo;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/** \brief Stores strategy information about each face in this namespace
 */
class NamespaceInfo : public StrategyInfo
{
public:
  static constexpr int
  getTypeId()
  {
    return 2000;
  }

  explicit
  NamespaceInfo(shared_ptr<const ndn::util::RttEstimator::Options> opts)
    : m_rttEstimatorOpts(std::move(opts))
  {
  }

  FaceInfo*
  getFaceInfo(FaceId faceId);

  FaceInfo&
  getOrCreateFaceInfo(FaceId faceId);

  void
  extendFaceInfoLifetime(FaceInfo& info, FaceId faceId);

  std::unordered_map<FaceId, FaceInfo> m_fiMap;
private:
  shared_ptr<const ndn::util::RttEstimator::Options> m_rttEstimatorOpts;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/** \brief Helper class to retrieve and create strategy measurements
 */
class MineMeasurements : noncopyable
{
public:
  explicit
  MineMeasurements(MeasurementsAccessor& measurements);

  FaceInfo*
  getFaceInfo(const fib::Entry& fibEntry, const Interest& interest, FaceId faceId);

  FaceInfo&
  getOrCreateFaceInfo(const fib::Entry& fibEntry, const Interest& interest, FaceId faceId);

  NamespaceInfo*
  getNamespaceInfo(const Name& prefix);

  NamespaceInfo&
  getOrCreateNamespaceInfo(const fib::Entry& fibEntry, const Interest& interest);

private:
  void
  extendLifetime(measurements::Entry& me);

public:
  static constexpr time::microseconds MEASUREMENTS_LIFETIME = 5_min;

private:
  MeasurementsAccessor& m_measurements;
  shared_ptr<const ndn::util::RttEstimator::Options> m_rttEstimatorOpts;
};

} // namespace mine2
} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_MINE_MEASUREMENTS_HPP
