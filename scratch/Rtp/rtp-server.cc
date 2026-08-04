#include "rtp-server.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/inet-socket-address.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include <iostream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RtpServer");
NS_OBJECT_ENSURE_REGISTERED (RtpServer);

TypeId
RtpServer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::RtpServer")
    .SetParent<Application> ()
    .SetGroupName ("Applications")
    .AddConstructor<RtpServer> ()
    .AddAttribute ("Local", "The Address on which to Bind the rx socket.",
                   AddressValue (),
                   MakeAddressAccessor (&RtpServer::m_local),
                   MakeAddressChecker ())
    .AddAttribute ("ForwardRemote",
                   "If set, relay received UDP/RTP payload to this address after parsing.",
                   AddressValue (),
                   MakeAddressAccessor (&RtpServer::m_forwardRemote),
                   MakeAddressChecker ());

  return tid;
}

RtpServer::RtpServer ()
  : m_enableForward (false),
    m_received (0),
    m_forwarded (0),
    m_lastSeq (0),
    m_haveLastSeq (false)
{
  NS_LOG_FUNCTION (this);
}

RtpServer::~RtpServer ()
{
  NS_LOG_FUNCTION (this);
}

bool
RtpServer::ParseRtpHeader (Ptr<Packet> packet, uint16_t &seq, uint32_t &ts,
                           uint32_t &ssrc, uint8_t &pt) const
{
  if (packet->GetSize () < 12)
    {
      return false;
    }
  uint8_t hdr[12];
  packet->CopyData (hdr, 12);
  if ((hdr[0] >> 6) != 2)
    {
      return false;
    }
  pt = hdr[1] & 0x7f;
  seq = static_cast<uint16_t> ((hdr[2] << 8) | hdr[3]);
  ts = (hdr[4] << 24) | (hdr[5] << 16) | (hdr[6] << 8) | hdr[7];
  ssrc = (hdr[8] << 24) | (hdr[9] << 16) | (hdr[10] << 8) | hdr[11];
  return true;
}

void
RtpServer::StartApplication ()
{
  NS_LOG_FUNCTION (this);

  m_enableForward = !m_forwardRemote.IsInvalid ();
  m_received = 0;
  m_forwarded = 0;
  m_haveLastSeq = false;

  if (m_socket == 0)
    {
      m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
      m_socket->Bind (m_local);
      m_socket->SetRecvCallback (MakeCallback (&RtpServer::HandleRead, this));
    }

  m_statsEvent = Simulator::Schedule (Seconds (1.0), &RtpServer::ReportStats, this);
}

void
RtpServer::StopApplication ()
{
  NS_LOG_FUNCTION (this);
  Simulator::Cancel (m_statsEvent);
  NS_LOG_UNCOND ("RtpServer on node " << GetNode ()->GetId ()
                 << " stopped; received=" << m_received
                 << " forwarded=" << m_forwarded);
  if (m_socket)
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
      m_socket = 0;
    }
}

void
RtpServer::HandleRead (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  Address from;
  while ((packet = socket->RecvFrom (from)))
    {
      ++m_received;
      const InetSocketAddress fromAddr = InetSocketAddress::ConvertFrom (from);

      uint16_t seq = 0;
      uint32_t ts = 0;
      uint32_t ssrc = 0;
      uint8_t pt = 0;
      if (ParseRtpHeader (packet, seq, ts, ssrc, pt))
        {
          if (m_haveLastSeq)
            {
              const uint16_t diff = static_cast<uint16_t> (seq - m_lastSeq);
              if (diff > 1 && diff < 0x8000)
                {
                  NS_LOG_UNCOND ("RtpServer node " << GetNode ()->GetId ()
                                 << " seq gap: last=" << m_lastSeq << " now=" << seq);
                }
            }
          m_lastSeq = seq;
          m_haveLastSeq = true;

          static uint32_t logCount = 0;
          if (++logCount <= 20 || (logCount % 100) == 0)
            {
              std::cout << "[RtpServer node " << GetNode ()->GetId () << "] from "
                        << fromAddr.GetIpv4 () << " seq=" << seq << " ts=" << ts
                        << " ssrc=0x" << std::hex << ssrc << std::dec
                        << " pt=" << static_cast<uint32_t> (pt)
                        << " bytes=" << packet->GetSize () << std::endl;
            }
        }
      else
        {
          NS_LOG_INFO ("Non-RTP UDP from " << fromAddr.GetIpv4 ()
                       << " size=" << packet->GetSize ());
        }

      if (m_enableForward)
        {
          int sent = socket->SendTo (packet, 0, m_forwardRemote);
          if (sent >= 0)
            {
              ++m_forwarded;
            }
          else
            {
              NS_LOG_WARN ("RtpServer forward to " << m_forwardRemote << " failed");
            }
        }
    }
}

void
RtpServer::ReportStats ()
{
  NS_LOG_UNCOND ("RtpServer node " << GetNode ()->GetId ()
                 << " stats: received=" << m_received
                 << " forwarded=" << m_forwarded);
  m_statsEvent = Simulator::Schedule (Seconds (1.0), &RtpServer::ReportStats, this);
}

} // namespace ns3
