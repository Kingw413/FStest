#include "ccaf.hpp"
#include "common/logger.hpp"
#include "ndn-wifi-net-device-transport.hpp"
#include "ns3/mobility-model.h"
#include "ns3/ndnSIM/NFD/daemon/fw/algorithm.hpp"
#include "ns3/ndnSIM/model/ndn-net-device-transport.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/apps/ndn-producer.hpp"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/model/ndn-common.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/cs.hpp"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/ptr.h"
#include <cmath>
#include <chrono>

namespace nfd {
    namespace fw {
        namespace ccaf {
            NFD_LOG_INIT(CCAF);
            NFD_REGISTER_STRATEGY(CCAF);

            const double CCAF::Rth(200.0);
            const double CCAF::Pth(0.8);
            const double CCAF::T(1.0);
            const int CCAF::CONTENT_NUM(50);
            const int CCAF::CACHE_SIZE(20);

            const time::milliseconds CCAF::RETX_SUPPRESSION_INITIAL(10);
            const time::milliseconds CCAF::RETX_SUPPRESSION_MAX(250);

            CCAF::CCAF(Forwarder& forwarder, const Name& name)
                : Strategy(forwarder),
                m_nodes(ns3::NodeContainer::GetGlobal()),
                m_measurements(getMeasurements()),
                m_retxSuppression(RETX_SUPPRESSION_INITIAL,
                    RetxSuppressionExponential::DEFAULT_MULTIPLIER,
                    RETX_SUPPRESSION_MAX) {
                ParsedInstanceName parsed = parseInstanceName(name);
                if (!parsed.parameters.empty()) {
                    NDN_THROW(std::invalid_argument("CCAF does not accept parameters"));
                }
                if (parsed.version &&
                    *parsed.version != getStrategyName()[-1].toVersion()) {
                    NDN_THROW(std::invalid_argument("CCAF does not support version " +
                        to_string(*parsed.version)));
                }
                this->setInstanceName(makeInstanceName(name, getStrategyName()));
                ns3::Simulator::Schedule(ns3::Seconds(1.0), &CCAF::distributeCLT, this);
            }

            const Name& CCAF::getStrategyName() {
                static Name strategyName("/localhost/nfd/strategy/CCAF/%FD%01");
                return strategyName;
            }

            void CCAF::afterReceiveInterest(const FaceEndpoint& ingress,
                const Interest& interest,
                const shared_ptr<pit::Entry>& pitEntry) {
                const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
                const Name prefix = fibEntry.getPrefix();
                const fib::NextHopList& nexthops = fibEntry.getNextHops();
                ns3::Ptr<ns3::Node> localNode = getNode(*this);
                std::set<ns3::Ptr<ns3::Node>> sources = this->getContentSources(interest);
                std::set<Face*> candidates = this->getCandidateForwarders(nexthops, localNode, sources);

                auto it = this->selectFIB(localNode, interest, candidates, fibEntry);
                if (it == nullptr) {
                    // NFD_LOG_DEBUG("No Next Hop!");
                    return;
                }
                auto egress = FaceEndpoint(*it, 0);
                NFD_LOG_DEBUG("do Send Interest=" << interest << " from=" << ingress << ", to=" << egress);
                this->sendInterest(pitEntry, egress, interest);
                FaceInfo& faceInfo = m_measurements.getOrCreateFaceInfo(fibEntry, interest, egress.face.getId());

                // Refresh measurements since Face is being used for forwarding
                NamespaceInfo& namespaceInfo = m_measurements.getOrCreateNamespaceInfo(fibEntry, interest);
                namespaceInfo.extendFaceInfoLifetime(faceInfo, egress.face.getId());
                ++faceInfo.m_counters.nOutInterests;

                auto currentTime = ndn::time::steady_clock::now();
                auto seconds = time::duration_cast<time::seconds>(currentTime.time_since_epoch()).count();
                double doubleTime = static_cast<double>(seconds);
                this->updateCLT(interest.getName(), doubleTime);
            }

            void
                CCAF::beforeSatisfyInterest(const shared_ptr<pit::Entry>& pitEntry,
                    const FaceEndpoint& ingress, const Data& data) {

                // NFD_LOG_DEBUG("beforeSatisfyInterest pitEntry=" << pitEntry->getName()
                //             << " in=" << ingress << " data=" << data.getName());

                NamespaceInfo* namespaceInfo = m_measurements.getNamespaceInfo(pitEntry->getName());
                if (namespaceInfo == nullptr) {
                    return;
                }

                // Record the RTT between the Interest out to Data in
                FaceInfo* faceInfo = namespaceInfo->getFaceInfo(ingress.face.getId());
                if (faceInfo == nullptr) {
                    return;
                }

                auto outRecord = pitEntry->getOutRecord(ingress.face);
                if (outRecord == pitEntry->out_end()) {
                }
                else {
                    faceInfo->recordRtt(time::steady_clock::now() - outRecord->getLastRenewed());
                    ++faceInfo->m_counters.nSatisfiedInterests;
                    double isr = faceInfo->m_counters.nOutInterests == 0.0 ? 0.0 : faceInfo->m_counters.nSatisfiedInterests / faceInfo->m_counters.nOutInterests;
                    faceInfo->recordISR(isr);
                    // NFD_LOG_DEBUG(pitEntry->getName() << " data from=" << ingress
                    //                                   <<" isr="<<faceInfo->getLastISR()<<" sisr="<<faceInfo->getSmoothedISR()
                    //                                   << " rtt=" << faceInfo->getLastRtt() << " srtt=" << faceInfo->getSrtt());
                }

                // Extend lifetime for measurements associated with Face
                namespaceInfo->extendFaceInfoLifetime(*faceInfo, ingress.face.getId());
            }


            void
                CCAF::afterContentStoreHit(const shared_ptr<pit::Entry>& pitEntry,
                    const FaceEndpoint& ingress, const Data& data) {
                // NFD_LOG_DEBUG("afterContentStoreHit pitEntry=" << pitEntry->getName()
                //             << " in=" << ingress << " data=" << data.getName());

                this->sendData(pitEntry, data, ingress);
                NFD_LOG_DEBUG("do Send Data=" << data.getName() << ", from=" << ingress);
            }

            void
                CCAF::afterReceiveData(const shared_ptr<pit::Entry>& pitEntry,
                    const FaceEndpoint& ingress, const Data& data) {
                this->beforeSatisfyInterest(pitEntry, ingress, data);
                // NFD_LOG_DEBUG("afterReceiveData pitEntry=" << pitEntry->getName()
                //             << " in=" << ingress << " data=" << data.getName());
                this->sendDataToAll(pitEntry, ingress, data);
                NFD_LOG_DEBUG("do Send Data=" << data.getName() << ", from=" << ingress);
            }

            bool
                CCAF::isProducer(ns3::Ptr<ns3::Node> node) {
                if (node->GetNApplications() == 0) {
                    return false;
                }
                for (uint32_t i = 0; i < node->GetNApplications(); i++) {
                    auto app = node->GetApplication(i);
                    ns3::Ptr<ns3::ndn::Producer> producer = app->GetObject<ns3::ndn::Producer>();
                    if (producer) {
                        return true;
                    }
                }
                return false;
            }

            std::set<ns3::Ptr<ns3::Node>>
                CCAF::getContentSources(const Interest& interest) {
                std::set<ns3::Ptr<ns3::Node>> sources;
                for (auto& node : m_nodes) {
                    if (isProducer(node)) {
                        sources.emplace(node);
                        continue;
                    }

                    auto currentTime = ndn::time::steady_clock::now();
                    auto seconds = time::duration_cast<time::seconds>(currentTime.time_since_epoch()).count();
                    double time = static_cast<double>(seconds);
                    double prob = cachePrediction(node, interest.getName(), time);
                    // double prob = 0.9;

                    bool isCached = false;
                    ns3::Ptr<ns3::ndn::L3Protocol> ndn = node->GetObject<ns3::ndn::L3Protocol>();
                    ndn::Name name = interest.getName();
                    nfd::cs::Cs& cs = ndn->getForwarder()->getCs();
                    for (const auto& entry : cs) {
                        if (entry.canSatisfy(interest)) {
                            // std::cout << "Content found in node " << node->GetId() << std::endl;
                            // sources.emplace(node);
                            isCached = true;
                            break;
                        }
                    }

                    if (prob > Pth) {
                        sources.emplace(node);
                    }

                    if ( (isCached && prob>Pth) || (!isCached && prob<Pth) ) {
                        cout<<"Cache Prediction True"<<endl;
                    }
                    else {
                        cout<<"Cache Prediction False"<<endl;
                    }
                }
                return sources;
            }

            std::set<Face*>
                CCAF::getCandidateForwarders(const fib::NextHopList& nexthops, ns3::Ptr<ns3::Node> curNode, std::set<ns3::Ptr<ns3::Node>> srcNodes) {
                std::set<Face*> inRegionSrcs;
                std::set<Face*> candidateForwarders;
                for (auto& srcNode : srcNodes) {
                    double d_sd = ns3::CalculateDistance(curNode->GetObject<ns3::MobilityModel>()->GetPosition(), srcNode->GetObject<ns3::MobilityModel>()->GetPosition());
                    for (auto& nexthop : nexthops) {
                        // Directly return App Face if it is Producer;
                        if (nexthop.getFace().getId() == 256 + m_nodes.GetN()) {
                            return std::set<Face*>{&nexthop.getFace()};
                        }
                        uint32_t faceId = nexthop.getFace().getId();
                        uint32_t nodeId = (faceId - 257) + (curNode->GetId() + 257 <= faceId);
                        ns3::Ptr<ns3::Node> node = m_nodes.Get(nodeId);
                        double d_sj = ns3::CalculateDistance(curNode->GetObject<ns3::MobilityModel>()->GetPosition(), node->GetObject<ns3::MobilityModel>()->GetPosition());
                        double d_jd = ns3::CalculateDistance(node->GetObject<ns3::MobilityModel>()->GetPosition(), srcNode->GetObject<ns3::MobilityModel>()->GetPosition());
                        // 判断是否是Content Source
                        if (d_sd < Rth) {
                            if (nodeId == srcNode->GetId())
                                inRegionSrcs.emplace(&nexthop.getFace());
                            // NFD_LOG_DEBUG("Source="<<srcNode->GetId()<<" in region");
                        }
                        else if (d_sj < Rth&& d_jd <= d_sd) {
                            candidateForwarders.emplace(&nexthop.getFace());
                            // NFD_LOG_DEBUG("Source=" << srcNode->GetId()<<", Candidate="<<nodeId<< ", d_sd=" << d_sd << ", d_sj=" << d_sj << ", d_jd=" << d_jd);
                        }
                        else {
                            // NFD_LOG_DEBUG("Source=" << srcNode->GetId() << ", Node=" << nodeId <<" is not candidate"<< ", d_sd=" << d_sd << ", d_sj=" << d_sj << ", d_jd=" << d_jd);
                        }
                    }
                }
                return inRegionSrcs.size() > 0 ? inRegionSrcs : candidateForwarders;
            }

            Face*
                CCAF::selectFIB(ns3::Ptr<ns3::Node> localNode, const Interest& interest, std::set<Face*> candidateForwarders, const fib::Entry& fibEntry) {
                std::vector<FaceStats> faceList;
                for (auto& face : candidateForwarders) {
                    if (face->getId() == 256 + m_nodes.GetN()) {
                        return face;
                    }
                    uint32_t nodeId = (face->getId() - 257) + (localNode->GetId() + 257 <= face->getId());
                    // double distance = this->calculateDistance(localNode, m_nodes[nodeId]);
                    double distance = this->caculateDR(localNode, m_nodes[nodeId]);
                    FaceInfo* info = m_measurements.getFaceInfo(fibEntry, interest, face->getId());
                    if (info == nullptr) {
                        faceList.push_back({ face, distance, 0, 0 });
                        // NFD_LOG_DEBUG("Face="<<face->getId()<<" has no Info");
                    }
                    else {
                        double sisr = info->getSmoothedISR();
                        double srtt = 10 - boost::chrono::duration_cast<boost::chrono::duration<double>>(info->getSrtt()).count(); // 正向化处理SRTT指标
                        faceList.push_back({ face, distance, sisr, srtt });
                        // NFD_LOG_DEBUG("Face=" << face->getId() << ", Distance=" << distance << ", SISR=" << sisr << ", SRTT=" << srtt);
                    }
                }

                if (faceList.empty()) {
                    // NFD_LOG_DEBUG("No Next Hop!");
                    return nullptr;
                }
                std::vector<FaceStats> normalizedFaceList = customNormalize(faceList);
                auto it = this->getOptimalDecision(normalizedFaceList);
                // auto it = std::max_element(normalizedFaceList.begin(), normalizedFaceList.end(), [](const auto& a, const auto& b) { return a.distance+a.sisr+a.srtt < b.distance+b.sisr+b.srtt; });

                // NFD_LOG_DEBUG("Selected Next Hop="<<it.face->getId()<<", Dis="<<it.distance<<", SISR="<<it.sisr<<", SRTT="<<it.srtt);
                return it.face;
            }

            std::vector<CCAF::FaceStats>
                CCAF::customNormalize(std::vector<FaceStats>& faceList) {
                double letSum = 0, lapSum = 0, srttSum = 0;
                std::vector<FaceStats> normalizedFaceList;
                for (const auto& faceStats : faceList) {
                    letSum += pow(faceStats.distance, 2);
                    lapSum += pow(faceStats.sisr, 2);
                    srttSum += pow(faceStats.srtt, 2);
                }
                for (auto& faceStats : faceList) {
                    Face* face = faceStats.face;
                    double let = letSum > 0 ? 1.0 / 3.0 * faceStats.distance / sqrt(letSum) : 0;
                    double lap = lapSum > 0 ? 1.0 / 3.0 * faceStats.sisr / sqrt(lapSum) : 0;
                    double srtt = srttSum > 0 ? 1.0 / 3.0 * faceStats.srtt / sqrt(srttSum) : 0;
                    normalizedFaceList.push_back({ face, let, lap, srtt });
                    // NFD_LOG_DEBUG("Face="<<face->getId()<<", nor_Distance="<<let<<", nor_SISR="<<lap<<", nor_SRTT="<<srtt);
                }
                return normalizedFaceList;
            }

            CCAF::FaceStats
                CCAF::calculateIdealSolution(std::vector<FaceStats>& faceList) {
                FaceStats idealSolution(
                    faceList[0].face, // 使用第一个节点作为默认值
                    (std::max_element(faceList.begin(), faceList.end(), [](const auto& a, const auto& b) { return a.distance < b.distance; }))
                    ->distance,
                    (std::max_element(faceList.begin(), faceList.end(), [](const auto& a, const auto& b) { return a.sisr < b.sisr; }))
                    ->sisr,
                    (std::max_element(faceList.begin(), faceList.end(), [](const auto& a, const auto& b) { return a.srtt < b.srtt; }))
                    ->srtt);
                // NFD_LOG_DEBUG("Ideal: D="<<idealSolution.distance<<", SISR="<<idealSolution.sisr<<", SRTT="<<idealSolution.srtt);
                return idealSolution;
            }

            CCAF::FaceStats
                CCAF::calculateNegativeIdealSolution(std::vector<FaceStats>& faceList) {
                FaceStats negativeIdealSolution(
                    faceList[0].face, // 使用第一个节点作为默认值
                    (std::min_element(faceList.begin(), faceList.end(), [](const auto& a, const auto& b) { return a.distance < b.distance; }))
                    ->distance,
                    (std::min_element(faceList.begin(), faceList.end(), [](const auto& a, const auto& b) { return a.sisr < b.sisr; }))
                    ->sisr,
                    (std::min_element(faceList.begin(), faceList.end(), [](const auto& a, const auto& b) { return a.srtt < b.srtt; }))
                    ->srtt);
                // NFD_LOG_DEBUG("Neg: D=" << negativeIdealSolution.distance << ", SISR=" << negativeIdealSolution.sisr << ", SRTT=" << negativeIdealSolution.srtt);
                return negativeIdealSolution;
            }

            double
                CCAF::calculateCloseness(const CCAF::FaceStats& entry, const CCAF::FaceStats& idealSolution, const CCAF::FaceStats& negativeIdealSolution) {
                double distanceIdealDeviation = entry.distance - idealSolution.distance;
                double relativeVelIdealDeviation = entry.sisr - idealSolution.sisr;
                double LETIdealDeviation = entry.srtt - idealSolution.srtt;
                double closenessToIdeal = sqrt(pow(distanceIdealDeviation, 2) + pow(relativeVelIdealDeviation, 2) + pow(LETIdealDeviation, 2));
                // NFD_LOG_DEBUG("disToIdeal="<<distanceIdealDeviation<<", velToIdeal="<<relativeVelIdealDeviation<<", letIdeal="<<LETIdealDeviation<<" clossToIdeal="<<closenessToIdeal);

                double distanceNegDeviation = entry.distance - negativeIdealSolution.distance;
                double relativeVelNegDeviation = entry.sisr - negativeIdealSolution.sisr;
                double LETNegDeviation = entry.srtt - negativeIdealSolution.srtt;
                double closenessToNeg = sqrt(pow(distanceNegDeviation, 2) + pow(relativeVelNegDeviation, 2) + pow(LETNegDeviation, 2));
                // NFD_LOG_DEBUG("disToNeg="<<distanceNegDeviation<<", velToIdeal="<<relativeVelNegDeviation<<", letIdeal="<<LETNegDeviation<<" clossToIdeal="<<closenessToNeg);

                double closeness = closenessToNeg / (closenessToIdeal + closenessToNeg);
                // NFD_LOG_DEBUG("node="<<entry.node->GetId()<<", closeness="<<closeness);

                return closeness;
            }

            CCAF::FaceStats&
                CCAF::getOptimalDecision(std::vector<CCAF::FaceStats>& faceList) {
                std::vector<double> closenessValues;
                FaceStats idealSolution = this->calculateIdealSolution(faceList);
                FaceStats negIdealSolution = this->calculateNegativeIdealSolution(faceList);
                for (const auto& entry : faceList) {
                    double closeness = this->calculateCloseness(entry, idealSolution, negIdealSolution);
                    closenessValues.push_back(closeness);
                }
                size_t optIndex = std::distance(closenessValues.begin(), std::max_element(closenessValues.begin(), closenessValues.end()));
                FaceStats& optimalDecision = faceList[optIndex];
                // NFD_LOG_DEBUG("Optimal Decision = " << optimalDecision.face->getId());
                return optimalDecision;
            }


            double
                CCAF::calculateDistance(ns3::Ptr<ns3::Node> node1, ns3::Ptr<ns3::Node> node2) {
                ns3::Ptr<ns3::MobilityModel> mobility1 = node1->GetObject<ns3::MobilityModel>();
                ns3::Ptr<ns3::MobilityModel> mobility2 = node2->GetObject<ns3::MobilityModel>();
                double d = mobility1->GetDistanceFrom(mobility2) + 0.0001;
                return d;
            }

            double
                CCAF::caculateDR(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> receiveNode) {
                double eculid = this->calculateDistance(sendNode, receiveNode);
                ns3::Ptr<ns3::MobilityModel> mobility = sendNode->GetObject<ns3::MobilityModel>();
                ns3::Vector3D nodePos = mobility->GetPosition();
                ns3::Ptr<ns3::MobilityModel> remoteMob = receiveNode->GetObject<ns3::MobilityModel>();
                ns3::Vector3D remotePos = remoteMob->GetPosition();
                // ns3::Vector3D direction = remoteMob->GetVelocity();
                ns3::Vector3D direction = { 1.0, 0.0, 0.0 };
                double angle = std::atan2(direction.x, direction.y) - std::atan2(remotePos.x - nodePos.x, remotePos.y - nodePos.y);
                double dr = abs(eculid * cos(angle));
                return dr;
            }

            double
                CCAF::calculateLET(ns3::Ptr<ns3::Node> sendNode, ns3::Ptr<ns3::Node> revNode) {
                ns3::Ptr<ns3::MobilityModel> mobility1 = sendNode->GetObject<ns3::MobilityModel>();
                ns3::Ptr<ns3::MobilityModel> mobility2 = revNode->GetObject<ns3::MobilityModel>();
                if (mobility1->GetDistanceFrom(mobility2) >= Rth) { return 0; }
                double m = mobility1->GetPosition().x - mobility2->GetPosition().x;
                double n = mobility1->GetPosition().y - mobility2->GetPosition().y;
                double p = mobility1->GetVelocity().x - mobility2->GetVelocity().x;
                double q = mobility1->GetVelocity().y - mobility2->GetVelocity().y;
                if (p == 0 && q == 0) { return 1e6; } //相对速度为0时，用1e6表示无限大
                double let = (-(m * p + n * q) + sqrt((pow(p, 2) + pow(q, 2)) * pow(Rth, 2) - pow(n * p - m * q, 2))) / (pow(p, 2) + pow(q, 2));
                return let;
            }

            ns3::Ptr<ns3::Node>
                CCAF::getNode(CCAF& local_strategy) {
                ns3::Ptr<ns3::Node> localNode;
                for (const auto& node : m_nodes) {
                    ns3::Ptr<ns3::ndn::L3Protocol> ndn = node->GetObject<ns3::ndn::L3Protocol>();
                    ndn::Name prefix("/ustc");
                    //此处有个坑：注册nfd时先是采用BestRouteStrategy2策略，其需要先发送set信息，因此需保证此时已经设置完毕策略
                    nfd::fw::Strategy& strategy = ndn->getForwarder()->getStrategyChoice().findEffectiveStrategy(prefix);
                    nfd::fw::ccaf::CCAF& CCAF_strategy = dynamic_cast<nfd::fw::ccaf::CCAF&>(strategy);
                    if (this == &CCAF_strategy) {
                        return node;
                    }
                }
            }

            void CCAF::updateCLT(ndn::Name name, double time) {
                auto it = find_if(m_CLT.begin(), m_CLT.end(), [&](auto& entry) { return entry.first == name; }); 
                if (it == m_CLT.end()) {
                    CCAF::CLT entry{1, time, 1.0/time, 1.0/m_ReqNums};
                    std::pair<ndn::Name, CCAF::CLT> pairs(name, entry);
                    m_CLT.push_back(pairs);
                }
                else {
                    CCAF::CLT& clt = it->second;
                    clt.lastTime = time;
                    ++clt.reqNums;
                    clt.popularity = m_ReqNums == 0 ? 0 : clt.reqNums / m_ReqNums;
                    clt.rate = time>0?  clt.reqNums / time : 0;
                }
            }

            void CCAF::distributeCLT() {
                m_distributed_CLT = m_CLT;
                sort(m_distributed_CLT.begin(), m_distributed_CLT.end(), [&](const auto& a, const auto& b) { return a.second.lastTime > b.second.lastTime; });
                ns3::Simulator::Schedule(ns3::Seconds(T), &CCAF::distributeCLT, this);
            }

            double CCAF::cachePrediction(ns3::Ptr<ns3::Node> node, const ndn::Name name, double time) {
                if (time < T) { return 0.0; }
                ns3::Ptr<ns3::ndn::L3Protocol> ndn = node->GetObject<ns3::ndn::L3Protocol>();
                ndn::Name prefix("/ustc");
                cout<<"predict node="<<node->GetId()<<endl;
                nfd::fw::Strategy& strategy = ndn->getForwarder()->getStrategyChoice().findEffectiveStrategy(prefix);
                cout<<strategy.getInstanceName()<<endl;
                nfd::fw::ccaf::CCAF& CCAF_strategy = dynamic_cast<nfd::fw::ccaf::CCAF&>(strategy);
                auto clt = CCAF_strategy.getCLT();
                cout<<"get clt"<<endl;
                auto it = std::find_if(clt.begin(), clt.end(), [&](auto entry) { return entry.first == name; });
                if (it == clt.end()) { return 0.0; }
                cout<<"find entry"<<endl;
                int order = std::distance(clt.begin(), it)+1;
                double prob;
                cout<<"order="<<order<<endl;
                double rate = it->second.rate;
                double rate_less = 0;
                double tau = time - int(time / T)*T;
                cout<<"rate="<<rate<<", tau="<<tau<<endl;
                double mu = 0.0, sigma = 0.0;
                if (order <= CACHE_SIZE) {
                    for (int i=order; i<clt.size(); i++) {
                        mu += exp(-clt[i].second.rate * tau);
                        sigma += exp(-clt[i].second.rate * tau) * (1 - exp(-clt[i].second.rate * tau));
                    }
                    cout<<"mu="<<mu<<", sigma="<<sigma<<endl;
                    prob = 0.5*erfc((CONTENT_NUM-CACHE_SIZE-mu) / sqrt(2) / sigma);
                    cout<<"case 1, prob="<<prob<<endl;
                }
                else{
                    double sum_rate = 0;
                    for (auto p : clt) {
                        sum_rate += p.second.rate;
                    }
                    prob = exp(-sum_rate*tau) * (exp(rate*tau)-1);
                    cout << "case 2, prob=" << prob << endl;
                }
                return prob;
            }

        }  // namespace ccaf
    }  // namespace fw
}  // namespace nfd