/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011-2018  Regents of the University of California.
 *
 * This file is part of ndnSIM. See AUTHORS for complete list of ndnSIM authors
 *and contributors.
 *
 * ndnSIM is free software: you can redistribute it and/or modify it under the
 *terms of the GNU General Public License as published by the Free Software
 *Foundation, either version 3 of the License, or (at your option) any later
 *version.
 *
 * ndnSIM is distributed in the hope that it will be useful, but WITHOUT ANY
 *WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 *A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndnSIM, e.g., in COPYING.md file.  If not, see
 *<http://www.gnu.org/licenses/>.
 **/

#include "ndn-wifi-net-device-transport-broadcast.hpp"

#include <ns3/ndnSIM/ndn-cxx/data.hpp>
#include <ns3/ndnSIM/ndn-cxx/encoding/block.hpp>
#include <ns3/ndnSIM/ndn-cxx/interest.hpp>

#include "ns3/ndnSIM//utils/ndn-ns3-packet-tag.hpp"
#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"
#include "ns3/ndnSIM/model/ndn-block-header.hpp"
#include "ns3/queue.h"
#include "ns3/wifi-mac.h"
#include "ns3/yans-wifi-channel.h"

NS_LOG_COMPONENT_DEFINE("ndn.WifiNetDeviceTransportBroadcast");

namespace ns3 {
namespace ndn {

WifiNetDeviceTransportBroadcast::WifiNetDeviceTransportBroadcast(
    Ptr<Node> node, const Ptr<WifiNetDevice>& netDevice,
    const std::string& localUri, const std::string& remoteUri,
    ::ndn::nfd::FaceScope scope, ::ndn::nfd::FacePersistency persistency,
    ::ndn::nfd::LinkType linkType)
    : m_netDevice(netDevice), m_node(node) {
    this->setLocalUri(FaceUri(localUri));
    this->setRemoteUri(FaceUri(remoteUri));
    this->setScope(scope);
    this->setPersistency(persistency);
    this->setLinkType(linkType);
    this->setMtu(m_netDevice->GetMtu());  // Use the MTU of the netDevice

    local_addr = getLocalUri().getHost().c_str();
    remote_addr = getRemoteUri().getHost().c_str();

    // Get send queue capacity for congestion marking
    PointerValue txQueueAttribute;
    if (m_netDevice->GetAttributeFailSafe("TxQueue", txQueueAttribute)) {
        NS_LOG_DEBUG("get wifi net device queue");
        Ptr<ns3::QueueBase> txQueue = txQueueAttribute.Get<ns3::QueueBase>();
        // must be put into bytes mode queue

        auto size = txQueue->GetMaxSize();
        NS_LOG_DEBUG("get wifi netdevice queue, size: " << size);
        if (size.GetUnit() == BYTES) {
            this->setSendQueueCapacity(size.GetValue());
        } else {
            // don't know the exact size in bytes, guessing based on "standard"
            // packet size
            this->setSendQueueCapacity(size.GetValue() * 1500);
        }
    }

    NS_LOG_FUNCTION(
        this << "Creating an ndnSIM transport instance for netDevice with URI #"
            // <<this->getFace()->getId()
            << this->getLocalUri()<<" -> "
            << this ->getRemoteUri());

    NS_ASSERT_MSG(m_netDevice != 0,
                  "NetDeviceFace needs to be assigned a valid NetDevice");

    m_node->RegisterProtocolHandler(
        MakeCallback(&WifiNetDeviceTransportBroadcast::receiveFromNetDevice, this),
        L3Protocol::ETHERNET_FRAME_TYPE, m_netDevice,
        true /*promiscuous mode*/);
}

WifiNetDeviceTransportBroadcast::~WifiNetDeviceTransportBroadcast() { NS_LOG_FUNCTION_NOARGS(); }

ssize_t WifiNetDeviceTransportBroadcast::getSendQueueLength() {
    PointerValue txQueueAttribute;
    if (m_netDevice->GetAttributeFailSafe("TxQueue", txQueueAttribute)) {
        Ptr<ns3::QueueBase> txQueue = txQueueAttribute.Get<ns3::QueueBase>();
        return txQueue->GetNBytes();
    } else {
        return nfd::face::QUEUE_UNSUPPORTED;
    }
}

void WifiNetDeviceTransportBroadcast::doClose() {
    NS_LOG_FUNCTION(this << "Closing transport for netDevice with URI"
                         << this->getLocalUri());

    // set the state of the transport to "CLOSED"
    this->setState(nfd::face::TransportState::CLOSED);
}

void WifiNetDeviceTransportBroadcast::doSend(const Block& packet,
                                    const nfd::EndpointId& endpoint) {
    NS_LOG_FUNCTION(this<< "Sending packet"
                         << this->getLocalUri()
                         <<"->"
                         <<this -> getRemoteUri());

    // convert NFD packet to NS3 packet
    BlockHeader header(packet);

    Ptr<ns3::Packet> ns3Packet = Create<ns3::Packet>();
    ns3Packet->AddHeader(header);

    // auto ch = DynamicCast<YansWifiChannel>(m_netDevice->GetChannel());

    // m_netDevice->GetMac();

    // send the NS3 packet
    m_netDevice->Send(ns3Packet, remote_addr,  // m_netDevice->GetBroadcast(),
                      L3Protocol::ETHERNET_FRAME_TYPE);
}

// callback
void WifiNetDeviceTransportBroadcast::receiveFromNetDevice(
    Ptr<NetDevice> device, Ptr<const ns3::Packet> p, uint16_t protocol,
    const Address& from, const Address& to, NetDevice::PacketType packetType) {

    // Convert NS3 packet to NFD packet
    Ptr<ns3::Packet> packet = p->Copy();

    BlockHeader header;
    packet->RemoveHeader(header);

    auto mac_from = Mac48Address::ConvertFrom(from);
    auto mac_to = Mac48Address::ConvertFrom(to);
    bool shouldup = (mac_from == remote_addr)&&(mac_to==local_addr);
    // bool shouldup = (mac_from == remote_addr);
    if (shouldup) {
        NS_LOG_DEBUG(mac_from << " == " << remote_addr <<" & "<< mac_to<<" == "<<local_addr <<"? it is " << shouldup);
        this->receive(std::move(header.getBlock()));
    }
}

Ptr<NetDevice> WifiNetDeviceTransportBroadcast::GetNetDevice() const {
    return m_netDevice;
}

Ptr<Node> WifiNetDeviceTransportBroadcast::GetNode() const {
    return m_node;
}

}  // namespace ndn
}  // namespace ns3
