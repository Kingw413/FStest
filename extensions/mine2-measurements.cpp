#include "mine2-measurements.hpp"
#include "common/global.hpp"

namespace nfd {
namespace fw {
namespace mine2 {

const time::nanoseconds FaceInfo::RTT_NO_MEASUREMENT{-1};
const time::nanoseconds FaceInfo::RTT_TIMEOUT{-2};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

FaceInfo*
NamespaceInfo::getFaceInfo(FaceId faceId)
{
  auto it = m_fiMap.find(faceId);
  return it != m_fiMap.end() ? &it->second : nullptr;
}

FaceInfo&
NamespaceInfo::getOrCreateFaceInfo(FaceId faceId)
{
  auto ret = m_fiMap.emplace(std::piecewise_construct,
                             std::forward_as_tuple(faceId),
                             std::forward_as_tuple(m_rttEstimatorOpts));
  auto& faceInfo = ret.first->second;
  if (ret.second) {
    extendFaceInfoLifetime(faceInfo, faceId);
  }
  return faceInfo;
}

void
NamespaceInfo::extendFaceInfoLifetime(FaceInfo& info, FaceId faceId)
{
  info.m_measurementExpiration = getScheduler().schedule(MineMeasurements::MEASUREMENTS_LIFETIME,
                                                         [=] { m_fiMap.erase(faceId); });
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

constexpr time::microseconds MineMeasurements::MEASUREMENTS_LIFETIME;

MineMeasurements::MineMeasurements(MeasurementsAccessor& measurements)
  : m_measurements(measurements)
  , m_rttEstimatorOpts(make_shared<ndn::util::RttEstimator::Options>())
{
}

FaceInfo*
MineMeasurements::getFaceInfo(const fib::Entry& fibEntry, const Interest& interest, FaceId faceId)
{
  return getOrCreateNamespaceInfo(fibEntry, interest).getFaceInfo(faceId);
}

FaceInfo&
MineMeasurements::getOrCreateFaceInfo(const fib::Entry& fibEntry, const Interest& interest,
                                     FaceId faceId)
{
  return getOrCreateNamespaceInfo(fibEntry, interest).getOrCreateFaceInfo(faceId);
}

NamespaceInfo*
MineMeasurements::getNamespaceInfo(const Name& prefix)
{
  measurements::Entry* me = m_measurements.findLongestPrefixMatch(prefix);
  if (me == nullptr) {
    return nullptr;
  }

  // Set or update entry lifetime
  extendLifetime(*me);

  NamespaceInfo* info = me->insertStrategyInfo<NamespaceInfo>(m_rttEstimatorOpts).first;
  BOOST_ASSERT(info != nullptr);
  return info;
}

NamespaceInfo&
MineMeasurements::getOrCreateNamespaceInfo(const fib::Entry& fibEntry, const Interest& interest)
{
  measurements::Entry* me = m_measurements.get(fibEntry);

  // If the FIB entry is not under the strategy's namespace, find a part of the prefix
  // that falls under the strategy's namespace
  for (size_t prefixLen = fibEntry.getPrefix().size() + 1;
       me == nullptr && prefixLen <= interest.getName().size(); ++prefixLen) {
    me = m_measurements.get(interest.getName().getPrefix(prefixLen));
  }

  // Either the FIB entry or the Interest's name must be under this strategy's namespace
  BOOST_ASSERT(me != nullptr);

  // Set or update entry lifetime
  extendLifetime(*me);

  NamespaceInfo* info = me->insertStrategyInfo<NamespaceInfo>(m_rttEstimatorOpts).first;
  BOOST_ASSERT(info != nullptr);
  return *info;
}

void
MineMeasurements::extendLifetime(measurements::Entry& me)
{
  m_measurements.extendLifetime(me, MEASUREMENTS_LIFETIME);
}

} // namespace mine
} // namespace fw
} // namespace nfd
