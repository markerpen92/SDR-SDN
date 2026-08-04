#include "rtp-client.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/inet-socket-address.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RtpClient");
NS_OBJECT_ENSURE_REGISTERED (RtpClient);

static const uint32_t RTP_HEADER_SIZE = 12;

TypeId RtpClient::GetTypeId (void) {
    static TypeId tid = TypeId ("ns3::RtpClient")
        .SetParent<Application> ()
        .SetGroupName ("Applications")
        .AddConstructor<RtpClient> ()
        .AddAttribute ("Remote", "The destination Address for the outbound packets.",
            AddressValue        (),
            MakeAddressAccessor (&RtpClient::m_peer),
            MakeAddressChecker  ()
        )
        .AddAttribute ("Interval", "Seconds between packet transmissions.",
            TimeValue (Seconds (0.1)),
            MakeTimeAccessor (&RtpClient::m_interval),
            MakeTimeChecker ()
        )
        .AddAttribute ("PacketSize", "RTP payload size in bytes (excluding 12-byte RTP header).",
            UintegerValue (512),
            MakeUintegerAccessor (&RtpClient::m_payloadSize),
            MakeUintegerChecker<uint32_t> (1)
        )
        .AddAttribute ("MaxPackets", "Maximum packets to send (0 means until app stop).",
            UintegerValue (0),
            MakeUintegerAccessor (&RtpClient::m_maxPackets),
            MakeUintegerChecker<uint32_t> ()
        )
        .AddAttribute ("PayloadType", "RTP payload type (PT).",
            UintegerValue (96),
            MakeUintegerAccessor (&RtpClient::m_payloadType),
            MakeUintegerChecker<uint8_t> ()
        )
        .AddAttribute ("Ssrc", "RTP synchronization source identifier.",
            UintegerValue (0x12345678),
            MakeUintegerAccessor (&RtpClient::m_ssrc),
            MakeUintegerChecker<uint32_t> ()
        );

    return tid;
}

RtpClient::RtpClient ()
    : m_interval (Seconds (0.1)),
      m_payloadSize (512),
      m_maxPackets (0),
      m_sent (0),
      m_seq (0),
      m_timestamp (0),
      m_ssrc (0x12345678),
      m_payloadType (96) {
    NS_LOG_FUNCTION (this);
}

RtpClient::~RtpClient () {
    NS_LOG_FUNCTION (this);
}

Ptr<Packet>
RtpClient::BuildRtpPacket () {
    Ptr<Packet> payload = Create<Packet> (m_payloadSize);
    uint8_t hdr[RTP_HEADER_SIZE];
    hdr[0] = 0x80;
    hdr[1] = m_payloadType & 0x7f;
    hdr[2] = (m_seq >> 8) & 0xff;
    hdr[3] = m_seq & 0xff;
    hdr[4] = (m_timestamp >> 24) & 0xff;
    hdr[5] = (m_timestamp >> 16) & 0xff;
    hdr[6] = (m_timestamp >> 8) & 0xff;
    hdr[7] = m_timestamp & 0xff;
    hdr[8] = (m_ssrc >> 24) & 0xff;
    hdr[9] = (m_ssrc >> 16) & 0xff;
    hdr[10] = (m_ssrc >> 8) & 0xff;
    hdr[11] = m_ssrc & 0xff;
    Ptr<Packet> packet = Create<Packet> (hdr, RTP_HEADER_SIZE);
    packet->AddAtEnd (payload);
    m_seq++;
    m_timestamp += 160;
    return packet;
}

void RtpClient::StartApplication () {
    NS_LOG_FUNCTION (this);

    if (m_peer.IsInvalid ()) {
        NS_FATAL_ERROR ("RtpClient Remote address not set");
    }

    if (m_socket == 0) {
        TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
        m_socket = Socket::CreateSocket (GetNode (), tid);
        if (m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), 0)) == -1) {
            NS_FATAL_ERROR ("RtpClient failed to bind socket");
        }
        m_socket->SetAllowBroadcast (true);
    }

    m_sent = 0;
    m_seq = 0;
    m_timestamp = 0;
    m_sendEvent = Simulator::Schedule (Seconds (0.1), &RtpClient::SendRequest, this);
    m_statsEvent = Simulator::Schedule (Seconds (1.0), &RtpClient::ReportStats, this);
}

void RtpClient::StopApplication () {
    NS_LOG_FUNCTION (this);
    Simulator::Cancel (m_sendEvent);
    Simulator::Cancel (m_statsEvent);
    NS_LOG_UNCOND ("RtpClient stopped on node " << GetNode ()->GetId ()
                   << "; total RTP packets sent: " << m_sent);
    if (m_socket) {
        m_socket->Close ();
        m_socket = 0;
    }
}

void RtpClient::ReportStats () {
    NS_LOG_UNCOND ("RtpClient node " << GetNode ()->GetId () << ": sent " << m_sent
                   << " packets to " << InetSocketAddress::ConvertFrom (m_peer).GetIpv4 ());
    m_statsEvent = Simulator::Schedule (Seconds (1.0), &RtpClient::ReportStats, this);
}

void RtpClient::SendRequest () {
    NS_LOG_FUNCTION (this);

    if (m_maxPackets > 0 && m_sent >= m_maxPackets) {
        return;
    }

    Ptr<Packet> packet = BuildRtpPacket ();
    int ret = m_socket->SendTo (packet, 0, m_peer);
    if (ret >= 0) {
        m_sent++;
        if (m_sent <= 3) {
            NS_LOG_UNCOND ("RtpClient sent packet #" << m_sent << " ("
                         << (RTP_HEADER_SIZE + m_payloadSize) << " bytes) to "
                         << InetSocketAddress::ConvertFrom (m_peer).GetIpv4 ()
                         << " port " << InetSocketAddress::ConvertFrom (m_peer).GetPort ());
        }
    }
    else {
        NS_LOG_UNCOND ("RtpClient SendTo failed at packet " << (m_sent + 1));
    }

    ScheduleNext ();
}

void RtpClient::ScheduleNext () {
    if (m_maxPackets > 0 && m_sent >= m_maxPackets) {
        return;
    }
    m_sendEvent = Simulator::Schedule (m_interval, &RtpClient::SendRequest, this);
}

} // namespace ns3
