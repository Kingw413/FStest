#include "ns3/config.h"
#include "ns3/log.h"
#include "ns3/command-line.h"

#include "annotated-topology-reader-m.hpp"
#include "generic-link-service-m.hpp"

#include "ns3/constant-velocity-mobility-model.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/point-to-point-module.h"
#include "ns3/position-allocator.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/ptr.h"
#include "ns3/qos-txop.h"
#include "ns3/rectangle.h"
#include "ns3/ssid.h"
#include "ns3/wifi-net-device.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/wifi-phy-state-helper.h"
#include "ns3/ocb-wifi-mac.h"
#include "ns3/wifi-80211p-helper.h"
#include "ns3/wave-mac-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/ns2-mobility-helper.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/NFD/daemon/face/face-common.hpp"
#include "ns3/ndnSIM/apps/ndn-producer.hpp"
#include "ns3/ndnSIM/helper/ndn-link-control-helper.hpp"
#include "ns3/ndnSIM/helper/ndn-global-routing-helper.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/model/ndn-common.hpp"
#include "ns3/ndnSIM/utils/tracers/ndn-app-delay-tracer.hpp"
#include "ns3/ndnSIM/utils/tracers/ndn-cs-tracer.hpp"

#include <iostream>
#include <random>

NS_LOG_COMPONENT_DEFINE("WifiSimpleOcb");

namespace ns3
{
	int main(int num, vector<int> consumers, vector<int> producers, double popularity, double rate, double time, string trace, string delay_log)
	{
		uint32_t N = num;
		vector<int> ConsumerId = consumers;
		vector<int> ProducerId = producers;
		double Popularity = popularity;
		double Rate = rate;
		double Time = time;
		string MobilityTrace = trace;
		string DelayTrace = delay_log;

		NodeContainer nodes;
		nodes.Create(N);

		std::string phyMode("OfdmRate6Mbps");
		YansWifiPhyHelper wifiPhy;
		YansWifiChannelHelper channelHelper = YansWifiChannelHelper::Default();
		Ptr<YansWifiChannel> channel = channelHelper.Create();
		Ptr<LogDistancePropagationLossModel> lossModel = CreateObject<LogDistancePropagationLossModel>();
		lossModel->SetReference(1, 40.00);
		lossModel->SetPathLossExponent(1);
		channel->SetPropagationLossModel(lossModel);
		wifiPhy.Set("TxPowerStart", DoubleValue(0));
		wifiPhy.Set("TxPowerEnd", DoubleValue(0));
		wifiPhy.SetChannel(channel);
		// ns-3 supports generate a pcap trace
		wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11);
		NqosWaveMacHelper wifi80211pMac = NqosWaveMacHelper::Default();
		Wifi80211pHelper wifi80211p = Wifi80211pHelper::Default();

		wifi80211p.SetRemoteStationManager("ns3::ConstantRateWifiManager",
										   "DataMode", StringValue(phyMode),
										   "ControlMode", StringValue(phyMode));

		NetDeviceContainer devices = wifi80211p.Install(wifiPhy, wifi80211pMac, nodes);
		Ns2MobilityHelper ns2Mobiity = Ns2MobilityHelper(MobilityTrace);
		ns2Mobiity.Install();

		// Install NDN stack on all nodes
		extern shared_ptr<::nfd::Face> WifiApStaDeviceBroadcastCallback(
			Ptr<Node> node, Ptr<ndn::L3Protocol> ndn, Ptr<NetDevice> device);
		ndn::StackHelper ndnHelper;
		ndnHelper.AddFaceCreateCallback(WifiNetDevice::GetTypeId(),
										MakeCallback(&WifiApStaDeviceBroadcastCallback));

		ndnHelper.setCsSize(20);
		ndnHelper.InstallAll();

		ndn::StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/LSIF/%FD%01");

		// Installing Consumer
		// ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
		// consumerHelper.SetAttribute("Frequency", DoubleValue(Rate));
		// consumerHelper.SetAttribute("Randomize", StringValue("none"));
		ndn::AppHelper consumerHelper("ns3::ndn::ConsumerZipfMandelbrot");
		consumerHelper.SetAttribute("Frequency", DoubleValue(Rate));
		consumerHelper.SetAttribute("NumberOfContents", StringValue("50"));
		consumerHelper.SetAttribute("q", StringValue("0"));
		consumerHelper.SetAttribute("s", DoubleValue(Popularity));
		consumerHelper.SetPrefix("/ustc");
		NodeContainer consumerContainer;
		for (auto &id : ConsumerId)
		{
			consumerContainer.Add(nodes[id]);
		}
		consumerHelper.Install(consumerContainer);

		// Installing Producer
		ndn::AppHelper producer("ns3::ndn::Producer");
		producer.SetPrefix("/ustc");
		producer.SetAttribute("PayloadSize", UintegerValue(1024));
		NodeContainer producerContainer;
		for (auto &id : ProducerId)
		{
			producerContainer.Add(nodes[id]);
		}
		producer.Install(producerContainer);

		std::cout << "Trace=" << MobilityTrace << ", Num=" << N << std::endl;
		std::cout << "Install " << consumerContainer.GetN() << " consumers on Node=";
		for (auto &consumer : consumerContainer)
		{
			std::cout << consumer->GetId() << ", ";
		}
		std::cout << " and " << producerContainer.GetN() << " producers on Node=";
		for (auto &producer : producerContainer)
		{
			std::cout << producer->GetId() << ", ";
		}
		std::cout << std::endl;

		ndn::AppDelayTracer::Install(consumerContainer, DelayTrace);
		// ndn::CsTracer::InstallAll("results/cs_prfs.log", MilliSeconds(1000));

		Simulator::Stop(Seconds(Time));
		Simulator::Run();
		Simulator::Destroy();
		std::cout << "end";
		return 0;
	}
}

// 解析字符串形式的列表参数，返回vector<int>
std::vector<int> parseList(const std::string &str)
{
	std::vector<int> result;
	std::istringstream ss(str.substr(1, str.size() - 2)); // 去掉首尾的方括号
	std::string token;
	while (std::getline(ss, token, ','))
	{
		result.push_back(std::stoi(token));
	}
	return result;
}

int main(int argc, char *argv[])
{
	// 创建命令行对象
	ns3::CommandLine cmd;
	int num;
	string consumers_list;
	string producers_list;
	double rate;
	double time;
	double popularity;
	string trace, log, delay_log;
	// 添加自定义参数
	cmd.AddValue("num", "Description for number of nodes parameter", num);
	cmd.AddValue("consumers", "List of consumer nodes", consumers_list);
	cmd.AddValue("producers", "List of producer nodes", producers_list);
	cmd.AddValue("popularity", "Popularity of Zipf", popularity);
	cmd.AddValue("rate", "Description for request rate  parameter", rate);
	cmd.AddValue("time", "Description for request rate  parameter", time);
	cmd.AddValue("trace", "Description for mobility trace  parameter", trace);
	cmd.AddValue("delay_log", "Description for delay log parameter", delay_log);

	// 解析命令行参数
	cmd.Parse(argc, argv);

	std::vector<int> consumers = parseList(consumers_list);
	std::vector<int> producers = parseList(producers_list);

	return ns3::main(num, consumers, producers, popularity, rate, time, trace, delay_log);
}