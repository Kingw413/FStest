/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014-2019,  Regents of the University of California,
 *                           Arizona Board of Regents,
 *                           Colorado State University,
 *                           University Pierre & Marie Curie, Sorbonne University,
 *                           Washington University in St. Louis,
 *                           Beijing Institute of Technology,
 *                           The University of Memphis.
 *
 * This file is part of NFD (Named Data Networking Forwarding Daemon).
 * See AUTHORS.md for complete list of NFD authors and contributors.
 *
 * NFD is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NFD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NFD, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "opt-measurements.hpp"
#include "common/global.hpp"

namespace nfd {
namespace fw {
namespace opt {

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
  info.m_measurementExpiration = getScheduler().schedule(OptMeasurements::MEASUREMENTS_LIFETIME,
                                                         [=] { m_fiMap.erase(faceId); });
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

constexpr time::microseconds OptMeasurements::MEASUREMENTS_LIFETIME;

OptMeasurements::OptMeasurements(MeasurementsAccessor& measurements)
  : m_measurements(measurements)
  , m_rttEstimatorOpts(make_shared<ndn::util::RttEstimator::Options>())
{
}

FaceInfo*
OptMeasurements::getFaceInfo(const fib::Entry& fibEntry, const Interest& interest, FaceId faceId)
{
  return getOrCreateNamespaceInfo(fibEntry, interest).getFaceInfo(faceId);
}

FaceInfo&
OptMeasurements::getOrCreateFaceInfo(const fib::Entry& fibEntry, const Interest& interest,
                                     FaceId faceId)
{
  return getOrCreateNamespaceInfo(fibEntry, interest).getOrCreateFaceInfo(faceId);
}

NamespaceInfo*
OptMeasurements::getNamespaceInfo(const Name& prefix)
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
OptMeasurements::getOrCreateNamespaceInfo(const fib::Entry& fibEntry, const Interest& interest)
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
OptMeasurements::extendLifetime(measurements::Entry& me)
{
  m_measurements.extendLifetime(me, MEASUREMENTS_LIFETIME);
}

} // namespace opt
} // namespace fw
} // namespace nfd
