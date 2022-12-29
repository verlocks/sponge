#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    EthernetFrame frame;

    // Search the Ethernet address in the ARP table.
    auto iter = _arp_table.find(next_hop_ip);
    // If the destination Ethernet address is already known, send it right away.
    if (iter != _arp_table.end()) {
        frame.header() = {
            iter->second.eth_address,
            _ethernet_address,
            EthernetHeader::TYPE_IPv4,
        };
        frame.payload() = dgram.serialize();
        _frames_out.emplace(frame);
        return;
    }

    // If the destination Ethernet address is unknown,
    // broadcast an ARP request for the next hop’s Ethernet address,
    // and queue the IP datagram, so it can be sent after the ARP reply is received.
    if (_waiting_arp_request_map.find(next_hop_ip) == _waiting_arp_request_map.end()) {
        ARPMessage arp_message;
        arp_message.opcode = ARPMessage::OPCODE_REQUEST;
        arp_message.sender_ethernet_address = _ethernet_address;
        arp_message.sender_ip_address = _ip_address.ipv4_numeric();
        arp_message.target_ethernet_address = {};
        arp_message.target_ip_address = next_hop_ip;
        frame.header() = {
            ETHERNET_BROADCAST,
            _ethernet_address,
            EthernetHeader::TYPE_ARP,
        };
        frame.payload() = arp_message.serialize();
        _frames_out.emplace(frame);

        _waiting_arp_request_map[next_hop_ip] = ARP_REQUEST_DEFAULT_TTL;
    }
    _waiting_datagrams.emplace_back(next_hop, dgram);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // Ignore any frames not destined for the network interface.
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return nullopt;
    }

    // If the inbound frame is IPv4
    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram datagram;
        if (datagram.parse(frame.payload()) != ParseResult::NoError)
            return nullopt;
        return datagram;
    } else if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_message;
        if (arp_message.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }

        const uint32_t &src_ip_address = arp_message.sender_ip_address;
        const uint32_t &dst_ip_address = arp_message.target_ip_address;
        const EthernetAddress &src_eth_address = arp_message.sender_ethernet_address;
        const EthernetAddress &dst_eth_address = arp_message.target_ethernet_address;

        bool is_arp_request =
            arp_message.opcode == ARPMessage::OPCODE_REQUEST && dst_ip_address == _ip_address.ipv4_numeric();
        bool is_arp_response = arp_message.opcode == ARPMessage::OPCODE_REPLY && dst_eth_address == _ethernet_address;
        // If get arp request, send response.
        if (is_arp_request) {
            ARPMessage arp_reply;
            arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.target_ethernet_address = src_eth_address;
            arp_reply.target_ip_address = src_ip_address;

            EthernetFrame reply_frame;
            reply_frame.header() = {
                src_eth_address,
                _ethernet_address,
                EthernetHeader::TYPE_ARP,
            };
            reply_frame.payload() = arp_reply.serialize();
            _frames_out.emplace(reply_frame);
        }

        if (is_arp_request || is_arp_response) {
            _arp_table[src_ip_address] = {src_eth_address, ARP_ENTRY_DEFAULT_TTL};
            for (auto iter = _waiting_datagrams.begin(); iter != _waiting_datagrams.end();) {
                if (iter->first.ipv4_numeric() == src_ip_address) {
                    send_datagram(iter->second, iter->first);
                    iter = _waiting_datagrams.erase(iter);
                } else
                    ++iter;
            }
            _waiting_arp_request_map.erase(src_ip_address);
        }
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    for (auto iter = _arp_table.begin(); iter != _arp_table.end(); /* nop */) {
        if (iter->second.ttl <= ms_since_last_tick)
            iter = _arp_table.erase(iter);
        else {
            iter->second.ttl -= ms_since_last_tick;
            ++iter;
        }
    }
    for (auto iter = _waiting_arp_request_map.begin(); iter != _waiting_arp_request_map.end(); /* nop */) {
        if (iter->second <= ms_since_last_tick) {
            ARPMessage arp_message;
            arp_message.opcode = ARPMessage::OPCODE_REQUEST;
            arp_message.sender_ethernet_address = _ethernet_address;
            arp_message.sender_ip_address = _ip_address.ipv4_numeric();
            arp_message.target_ethernet_address = {};
            arp_message.target_ip_address = iter->first;

            EthernetFrame eth_frame;
            eth_frame.header() = {/* dst  */ ETHERNET_BROADCAST,
                                  /* src  */ _ethernet_address,
                                  /* type */ EthernetHeader::TYPE_ARP};
            eth_frame.payload() = arp_message.serialize();
            _frames_out.push(eth_frame);

            iter->second = ARP_REQUEST_DEFAULT_TTL;
        } else {
            iter->second -= ms_since_last_tick;
            ++iter;
        }
    }
}
