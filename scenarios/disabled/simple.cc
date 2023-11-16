#include "annotated-topology-reader-m.hpp"
#include "generic-link-service-m.hpp"
#include "ns3/constant-velocity-mobility-model.h"
#include "ns3/core-module.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/mobility-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/NFD/daemon/face/face-common.hpp"
#include "ns3/ndnSIM/apps/ndn-consumer-batches.hpp"
#include "ns3/ndnSIM/helper/ndn-link-control-helper.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/utils/tracers/ndn-app-delay-tracer.hpp"
#include "ns3/ndnSIM/utils/tracers/ndn-cs-tracer.hpp"
#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/point-to-point-module.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/ptr.h"
#include "ns3/qos-txop.h"
#include "ns3/rectangle.h"
#include "ns3/ssid.h"
#include "ns3/wifi-helper.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-net-device.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/wifi-phy-state-helper.h"

NS_LOG_COMPONENT_DEFINE("ndn.WirelessSimulation");

std::ofstream  outRx1("Rx0.log");
std::ofstream  outRx2("Rx1.log");

namespace ns3 {


//位置回调函数
void showPosition(NodeContainer nodes, double deltaTime) {
    cout.precision(3);
    std::cout.setf(std::ios::fixed);
    for(NodeContainer::Iterator i = nodes.Begin(); i != nodes.End(); ++i){
        Ptr<Node> node = *i;
        string nodeName = Names::FindName(node);
        Ptr<MobilityModel> mobModel = node->GetObject<MobilityModel>();
        Vector3D pos = mobModel->GetPosition();
        Vector3D speed = mobModel->GetVelocity();
        std::cout << "Time " << Simulator::Now().GetSeconds() << " Node "
                << nodeName << ": Position(" << pos.x << ", " << pos.y << ", "
                << pos.z << ");   Speed(" << speed.x << ", " << speed.y << ", "
                << speed.z << ")" << std::endl;
    }
    Simulator::Schedule(Seconds(deltaTime), &showPosition, nodes, deltaTime);
}


//接收功率回调函数
void MyRxCallback1(Ptr<const Packet> packet, uint16_t channelFreqMhz, WifiTxVector txVector, MpduInfo aMpdu, SignalNoiseDbm signalNoise, uint16_t staId)
{
    double rssi = signalNoise.signal;
    double noise = signalNoise.noise;
    double snr = rssi - noise;
    // string logfile = "Rx" + to_string(staId)+".log";
    // ofstream outRx(logfile, ios::app);
    outRx1.precision(3); 
    outRx1.setf(std::ios::fixed);
    outRx1 << "Time " << Simulator::Now().GetSeconds() <<"\t"
    // <<"Tx Power = "<< txPower << "dBm, "
    <<"sta"<<staId
    <<" Received packet with RSSI = " << rssi << " dBm, Noise = " << noise << " dBm, SNR = " << snr << " dB" << std::endl;
    //  Simulator::Schedule(Seconds(1.0), &MyRxCallback,  packet,  channelFreqMhz,  txVector,  aMpdu,  signalNoise,  staId);
}

void MyRxCallback2(Ptr<const Packet> packet, uint16_t channelFreqMhz, WifiTxVector txVector, MpduInfo aMpdu, SignalNoiseDbm signalNoise, uint16_t staId)
{
    double rssi = signalNoise.signal;
    double noise = signalNoise.noise;
    double snr = rssi - noise;
    // string logfile = "Rx" + to_string(staId)+".log";
    // ofstream outRx(logfile, ios::app);
    outRx2.precision(3); 
    outRx2.setf(std::ios::fixed);
    outRx2 << "Time " << Simulator::Now().GetSeconds() <<"\t"
    // <<"Tx Power = "<< txPower << "dBm, "
    <<"sta"<<staId
    <<" Received packet with RSSI = " << rssi << " dBm, Noise = " << noise << " dBm, SNR = " << snr << " dB" << std::endl;
    //  Simulator::Schedule(Seconds(1.0), &MyRxCallback,  packet,  channelFreqMhz,  txVector,  aMpdu,  signalNoise,  staId);
}


int main(int argc, char* argv[]) {

    extern shared_ptr<::nfd::Face> WifiApStaDeviceCallback(
        Ptr<Node> node, Ptr<ndn::L3Protocol> ndn, Ptr<NetDevice> device);

    YansWifiPhyHelper phyHelper;
    NodeContainer adhocNodes;
    adhocNodes.Create(3);
    WifiHelper wifi;

    Names::Add("adhoc1", adhocNodes.Get(0));
    Names::Add("adhoc2", adhocNodes.Get(1));

    YansWifiChannelHelper channelHelper = YansWifiChannelHelper::Default();
    Ptr<YansWifiChannel> channel = channelHelper.Create();
    Ptr<LogDistancePropagationLossModel> lossModel = CreateObject<LogDistancePropagationLossModel>();
    lossModel->SetReference(1, 60.00);
    lossModel -> SetPathLossExponent(1);
    channel -> SetPropagationLossModel(lossModel);

    phyHelper.Set("TxPowerStart", DoubleValue(0));
    phyHelper.Set("TxPowerEnd", DoubleValue(0));

    phyHelper.SetChannel(channel);
    wifi.SetStandard(WIFI_STANDARD_80211n_5GHZ);
    phyHelper.Set("ChannelNumber", UintegerValue(38));
    phyHelper.Set("ChannelWidth", UintegerValue(40));


    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                                 StringValue("HtMcs7"), "ControlMode",
                                 StringValue("HtMcs7"));

    WifiMacHelper macHelper;
    macHelper.SetType("ns3::AdhocWifiMac");

    NetDeviceContainer adhocDevices;
    adhocDevices = wifi.Install(phyHelper, macHelper, adhocNodes);


    //设置初始位置
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>(); 
    positionAlloc->Add(Vector(0, 0, 0));
    positionAlloc -> Add(Vector(0,0,0));   
    positionAlloc -> Add(Vector(50,0,0));


    MobilityHelper mobility_STA;
    mobility_STA.SetPositionAllocator(positionAlloc);
    mobility_STA.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility_STA.Install(adhocNodes);


    // ConstantVelocity模型
    // Ptr<ConstantVelocityMobilityModel> mobility = CreateObject<ConstantVelocityMobilityModel>();
    // mobility->SetPosition(Vector(-50, 0, 0));
    // mobility->SetVelocity(Vector(10, 0, 0));
    // adhocNodes[0]->AggregateObject(mobility); 

    //Waypoint模型
    // Ptr<WaypointMobilityModel> mobility = CreateObject<WaypointMobilityModel>( );
    // for(int i=0;i<10;++i){
    //     double time = 0+2.001*i;
    //     double x = -pow(1.0233,i);
    //     mobility -> AddWaypoint(Waypoint(Seconds(time), Vector(x, 0, 0)));
    //     mobility -> AddWaypoint(Waypoint(Seconds(time+2.0), Vector(x, 0, 0)));
    // }
    // staNodes[1] -> AggregateObject(mobility);

    // Install NDN stack on all nodes
    ndn::StackHelper ndnHelper;
    ndnHelper.AddFaceCreateCallback(WifiNetDevice::GetTypeId(),
                                    MakeCallback(&WifiApStaDeviceCallback));
    ndnHelper.SetLinkDelayAsFaceMetric();
    ndnHelper.SetDefaultRoutes(true);
    ndnHelper.setCsSize(50);
    ndnHelper.InstallAll();
    std::cout << "Install stack\n";

    // Routing strategy
    ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
    ndnGlobalRoutingHelper.InstallAll();
    ndnGlobalRoutingHelper.AddOrigin("/ustc", adhocNodes[1]);
    ndnGlobalRoutingHelper.AddOrigin("/ustc/1", adhocNodes[2]);

    ndnGlobalRoutingHelper.CalculateRoutes();
    
    std::cout << "Install routing\n";

    ndn::StrategyChoiceHelper::InstallAll("/ustc",
                                          "/localhost/nfd/strategy/best-route");

    std::cout << "Install strategy\n";

    // Installing Consumer
    ndn::AppHelper consumer("ns3::ndn::ConsumerCbr");
    consumer.SetAttribute("Frequency", DoubleValue(10000.0));
    consumer.SetAttribute("Randomize", StringValue("none"));
    consumer.SetPrefix("/ustc/1");
    ApplicationContainer consumercontainer = consumer.Install(adhocNodes[0]);
    // consumer.SetPrefix("/ustc/2");
    // consumercontainer.Add(consumer.Install(adhocNodes[1]));
    // consumer.SetPrefix("/ustc/3");
    // consumercontainer.Add(consumer.Install(staNodes[2]));
    // consumer.SetPrefix("/ustc/4");
    // consumercontainer.Add(consumer.Install(staNodes[3]));
    std::cout << "Install consumer\n";

    // Installing Producer
    ndn::AppHelper producer("ns3::ndn::Producer");
    producer.SetPrefix("/ustc");
    producer.SetAttribute("PayloadSize", UintegerValue(1024));
    auto producercontainer = producer.Install(adhocNodes[1]);
    producer.SetPrefix("/ustc/1");
    producercontainer.Add(producer.Install(adhocNodes[2]));
    std::cout << "Install producer\n";

    std::cout << "Install consumers in " << consumercontainer.GetN()
              << " nodes and producers in " << producercontainer.GetN()
              << " nodes" << std::endl;

    ndn::AppDelayTracer::Install(adhocNodes[0], "delay0.log");
    // ndn::AppDelayTracer::Install(adhocNodes[1], "delay1.log");
    // ndn::AppDelayTracer::Install(staNodes[2], "delay2.log");
    // ndn::AppDelayTracer::Install(staNodes[3], "delay3.log");

    ndn::CsTracer::InstallAll("cs.log", MilliSeconds(1000));


    Simulator::Schedule(Seconds(0.0), &showPosition, adhocNodes, double(1.0));

    Ptr<WifiPhy> adhoc1Phy = adhocDevices.Get(0) -> GetObject<WifiNetDevice>( ) -> GetPhy( );
    Ptr<WifiPhy> adhoc2Phy = adhocDevices.Get(1) -> GetObject<WifiNetDevice>( ) -> GetPhy( );



    adhoc1Phy -> TraceConnectWithoutContext("MonitorSnifferRx", MakeCallback (&MyRxCallback1));
    adhoc2Phy -> TraceConnectWithoutContext("MonitorSnifferRx", MakeCallback (&MyRxCallback2));

    Simulator::Stop(Seconds(4));
    Simulator::Run();
    Simulator::Destroy();
    std::cout << "end" << std::endl;
    return 0;
}
}  // namespace ns3

int main(int argc, char* argv[]) { return ns3::main(argc, argv); }