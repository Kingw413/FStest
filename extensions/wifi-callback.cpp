#include "generic-link-service-m.hpp"
#include "ndn-wifi-net-device-transport.hpp"
#include "ns3/core-module.h"
#include "ns3/mobility-helper.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/NFD/daemon/face/face-common.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/model/ndn-net-device-transport.hpp"
#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/ptr.h"
#include "ns3/wifi-helper.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-net-device.h"
#include "ns3/yans-wifi-helper.h"

NS_LOG_COMPONENT_DEFINE("WIFICB");

namespace ns3 {

std::string constructFaceUri(Ptr<NetDevice> netDevice) {
    std::string uri = "netdev://";
    Address address = netDevice->GetAddress();
    if (Mac48Address::IsMatchingType(address)) {
        uri += "[" +
               boost::lexical_cast<std::string>(
                   Mac48Address::ConvertFrom(address)) +
               "]";
    }

    return uri;
}

shared_ptr<::nfd::face::Face> WifiApStaDeviceCallback(Ptr<Node> node,
                                                      Ptr<ndn::L3Protocol> ndn,
                                                      Ptr<NetDevice> device) {
    NS_LOG_DEBUG("Creating Wifi Face on node " << node->GetId());
    Ptr<WifiNetDevice> netDevice = DynamicCast<WifiNetDevice>(device);
    NS_ASSERT(netDevice != nullptr);

    // access the other end of the link
    Ptr<YansWifiChannel> channel =
        DynamicCast<YansWifiChannel>(netDevice->GetChannel());
    NS_ASSERT(channel != nullptr);

    Ptr<WifiMac> mac = netDevice->GetMac();
    NS_ASSERT(mac != nullptr);

    NetDeviceContainer apdevices;
    NetDeviceContainer stadevices;
    NetDeviceContainer adhocDevices;
    NetDeviceContainer vanetDevices;
    for (uint32_t i = 0; i < channel->GetNDevices(); i++) {
        auto ite_dev = DynamicCast<WifiNetDevice>(channel->GetDevice(i));
        if (ite_dev->GetMac()->GetTypeOfStation() == AP)
            apdevices.Add(ite_dev);
        if (ite_dev->GetMac()->GetTypeOfStation() == STA)
            stadevices.Add(ite_dev);
        if(ite_dev->GetMac()->GetTypeOfStation()== ADHOC_STA ) 
            adhocDevices.Add(ite_dev);
        if(ite_dev->GetMac()->GetTypeOfStation()== OCB ) 
            vanetDevices.Add(ite_dev);
    }

    NS_LOG_DEBUG("wifi channel have " 
                                      << vanetDevices.GetN()<< " vehicles");

    auto type = mac->GetTypeOfStation();
    NetDeviceContainer* remotedev;
    switch (type) {
    case AP:
        remotedev = &stadevices;
        break;
    case STA:
        remotedev = &apdevices;
        break;
    case ADHOC_STA:
        remotedev = &adhocDevices;
        break;
    case OCB:
        remotedev = &vanetDevices;
        break;
    default:
        NS_LOG_ERROR("do not support this wifi mac type, it should be AP/STA");
        exit(1);
        break;
    }

    shared_ptr<::nfd::face::Face> face;

    // Create an ndnSIM-specific transport instance
    for (uint32_t i = 0; i < remotedev->GetN(); i++) {
        ::nfd::face::GenericLinkServiceM::Options opts;
        opts.allowFragmentation = true;
        opts.allowReassembly = true;
        opts.allowCongestionMarking = true;

        auto linkService = make_unique<::nfd::face::GenericLinkServiceM>(opts);

        if(netDevice != remotedev->Get(i)){
            auto transport = make_unique<ndn::WifiNetDeviceTransport>(
                node, netDevice, constructFaceUri(netDevice),
                constructFaceUri(remotedev->Get(i)));

            face = std::make_shared<::nfd::face::Face>(std::move(linkService),
                                                    std::move(transport));
            face->setMetric(1);

            ndn->addFace(face);
            NS_LOG_LOGIC("Node " << node->GetId() << ": added Face #"
                                << face->getId()<<", "
                                << face->getLocalUri() << "-> "
                                << face->getRemoteUri());

            ndn::FibHelper::AddRoute(node,"/",face,1);
            NS_LOG_LOGIC("Node "<<node->GetId() <<"added FIB entry: "<<"/"<<face);
        }
    }
    return face;
}

}  // namespace ns3
