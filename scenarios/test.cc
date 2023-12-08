#include "ns3/vector.h"
#include "ns3/string.h"
#include "ns3/double.h"
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
#include "ns3/ndnSIM/apps/ndn-consumer-batches.hpp"
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

	int main(int argc, char *argv[])
	{
		std::string phyMode("OfdmRate6Mbps");

		NodeContainer nodes;
		nodes.Create(4);

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

		// Ns2MobilityHelper ns2Mobiity = Ns2MobilityHelper("/home/whd/ndnSIM2.8/wireless-macspec/scenarios/manhattan.tcl");
		// ns2Mobiity.Install();
		Ptr<ListPositionAllocator> positionAlloc =
			CreateObject<ListPositionAllocator>();
		positionAlloc->Add(Vector(0, 0, 0));
		positionAlloc->Add(Vector(40, 0, 0));
		positionAlloc->Add(Vector(80, 0, 0));
		positionAlloc->Add(Vector(120, 0, 0));

		MobilityHelper mobility_STA;
		mobility_STA.SetPositionAllocator(positionAlloc);
		// mobility_STA.SetMobilityModel("ns3::ConstantPositionMobilityModel");

	for (const auto& node:nodes) {
		    Ptr<ConstantVelocityMobilityModel> mobility = CreateObject<ConstantVelocityMobilityModel>();
    mobility->SetVelocity(Vector(10, 0, 0));
		node->AggregateObject(mobility);
	}

	// ConstantVelocityHelper mob;
	// mob.SetVelocity(Vector3D(10,0,0));
	

		mobility_STA.Install(nodes);
		//   Ptr<ListPositionAllocator> positionAlloc =
		//     CreateObject<ListPositionAllocator>();
		// positionAlloc->Add(Vector(0, 0, 0));
		// positionAlloc->Add(Vector(50, 0, 0));
		// positionAlloc->Add(Vector(90, 0, 0));
		// positionAlloc->Add(Vector(160, 0, 0));

		// MobilityHelper mobility_STA;
		// mobility_STA.SetPositionAllocator(positionAlloc);
		// mobility_STA.SetMobilityModel("ns3::ConstantPositionMobilityModel");
		// mobility_STA.Install(nodes);

		// Install NDN stack on all nodes
		extern shared_ptr<::nfd::Face> WifiApStaDeviceCallback(
			Ptr<Node> node, Ptr<ndn::L3Protocol> ndn, Ptr<NetDevice> device);
		ndn::StackHelper ndnHelper;
		ndnHelper.AddFaceCreateCallback(WifiNetDevice::GetTypeId(),
										MakeCallback(&WifiApStaDeviceCallback));
		// ndnHelper.SetLinkDelayAsFaceMetric();
		// ndnHelper.SetDefaultRoutes(true);

		ndnHelper.setCsSize(20);
		ndnHelper.InstallAll();
		std::cout << "Install stack\n";

		ndn::StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/MUPF/%FD%01");

		// Installing Consumer
		ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
		// ndn::AppHelper consumerHelper("ns3::ndn::ConsumerTest");
		consumerHelper.SetAttribute("Frequency", DoubleValue(1));
		consumerHelper.SetAttribute("Randomize", StringValue("none"));
		// ndn::AppHelper consumerHelper("ns3::ndn::ConsumerZipfMandelbrot");
		// consumerHelper.SetAttribute("Frequency", StringValue("10"));
		// consumerHelper.SetAttribute("NumberOfContents", StringValue("100"));
		// consumerHelper.SetAttribute("q", StringValue("0"));
		// consumerHelper.SetAttribute("s", StringValue("0.7"));
		consumerHelper.SetPrefix("/ustc");
		NodeContainer consumerContainer;
		consumerContainer.Add(nodes[1]);
		// consumerContainer.Add(nodes[2]);
		consumerHelper.Install(consumerContainer);
		std::cout << "Install consumer\n";

		// Installing Producer
		ndn::AppHelper producer("ns3::ndn::Producer");
		producer.SetPrefix("/ustc");
		producer.SetAttribute("PayloadSize", UintegerValue(1024));
		auto producercontainer = producer.Install(nodes[3]);
		producer.Install(nodes[0]);
		std::cout << "Install producer\n";
		std::cout << "Install consumers in " << consumerContainer.GetN()
				  << " nodes and producers in " << producercontainer.GetN()
				  << " nodes" << std::endl;

		ndn::AppDelayTracer::Install(nodes[1], "results/delay_vndn.log");
		// ndn::CsTracer::InstallAll("results/cs_prfs.log", MilliSeconds(1000));

		Simulator::Stop(Seconds(10));
		Simulator::Run();
		Simulator::Destroy();
		std::cout << "end" << std::endl;
		return 0;
	}
}

int main(int argc, char *argv[]) { return ns3::main(argc, argv); }