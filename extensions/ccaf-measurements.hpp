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

#ifndef NFD_DAEMON_FW_CCAF_MEASUREMENTS_HPP
#define NFD_DAEMON_FW_CCAF_MEASUREMENTS_HPP

#include "fw/strategy-info.hpp"
#include "fw/forwarder-counters.hpp"
#include "table/measurements-accessor.hpp"

#include <ndn-cxx/util/rtt-estimator.hpp>

namespace nfd {
    namespace fw {
        namespace ccaf {

            /** \brief Strategy information for each face in a namespace
            */
            class FaceInfo {
            public:
                explicit
                    FaceInfo(shared_ptr<const ndn::util::RttEstimator::Options> opts)
                    : m_rttEstimator(std::move(opts)) {
                }
                void
                    recordRtt(time::nanoseconds rtt) {
                    m_lastRtt = rtt;
                    m_rttEstimator.addMeasurement(rtt);
                }

                time::nanoseconds
                    getLastRtt() const {
                    return m_lastRtt;
                }

                time::nanoseconds
                    getSrtt() const {
                    return m_rttEstimator.getSmoothedRtt();
                }

                void
                    recordISR(double newISR) {
                    m_lastISR = newISR;
                    if (m_isr == 0.0) {
                        m_isr = newISR;
                    }
                    else {
                        m_isr = 0.5 * newISR + (1 - 0.5) * m_isr;
                    }
                }

                double
                    getLastISR() const {
                    return m_lastISR;
                }
                double getSmoothedISR() const {
                    return m_isr;
                }

            public:
                static const time::nanoseconds RTT_NO_MEASUREMENT;
                static const time::nanoseconds RTT_TIMEOUT;
                ForwarderCounters m_counters;

            private:
                ndn::util::RttEstimator m_rttEstimator;
                time::nanoseconds m_lastRtt = RTT_NO_MEASUREMENT;
                double m_isr = 0.0;
                double m_lastISR = 0.0;
                Name m_lastInterestName;

                // Timeout associated with measurement
                scheduler::ScopedEventId m_measurementExpiration;
                friend class NamespaceInfo;
            };

            ////////////////////////////////////////////////////////////////////////////////
            ////////////////////////////////////////////////////////////////////////////////

            /** \brief Stores strategy information about each face in this namespace
             */
            class NamespaceInfo : public StrategyInfo {
            public:
                static constexpr int
                    getTypeId() {
                    return 2000;
                }

                explicit
                    NamespaceInfo(shared_ptr<const ndn::util::RttEstimator::Options> opts)
                    : m_rttEstimatorOpts(std::move(opts)) {
                }

                FaceInfo*
                    getFaceInfo(FaceId faceId);

                FaceInfo&
                    getOrCreateFaceInfo(FaceId faceId);

                void
                    extendFaceInfoLifetime(FaceInfo& info, FaceId faceId);

            private:
                std::unordered_map<FaceId, FaceInfo> m_fiMap;
                shared_ptr<const ndn::util::RttEstimator::Options> m_rttEstimatorOpts;
            };

            ////////////////////////////////////////////////////////////////////////////////
            ////////////////////////////////////////////////////////////////////////////////

            /** \brief Helper class to retrieve and create strategy measurements
             */
            class CCAFMeasurements : noncopyable {
            public:
                explicit
                    CCAFMeasurements(MeasurementsAccessor& measurements);

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

        } // namespace ccaf
    } // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_CCAF_MEASUREMENTS_HPP
