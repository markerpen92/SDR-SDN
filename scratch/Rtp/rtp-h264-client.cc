#include "rtp-h264-client.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/inet-socket-address.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/string.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RtpH264Client");
NS_OBJECT_ENSURE_REGISTERED (RtpH264Client);

static const uint32_t RTP_HEADER_SIZE = 12;
static const uint32_t H264_CLOCK_RATE = 90000;

TypeId
RtpH264Client::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::RtpH264Client")
    .SetParent<Application> ()
    .SetGroupName ("Applications")
    .AddConstructor<RtpH264Client> ()
    .AddAttribute ("Remote", "Destination socket address.",
                   AddressValue (),
                   MakeAddressAccessor (&RtpH264Client::m_peer),
                   MakeAddressChecker ())
    .AddAttribute ("VideoFile", "Annex-B H.264 elementary stream file path.",
                   StringValue ("naruto.h264"),
                   MakeStringAccessor (&RtpH264Client::m_videoFile),
                   MakeStringChecker ())
    .AddAttribute ("Fps", "Video frame rate used for RTP timestamps.",
                   DoubleValue (25.0),
                   MakeDoubleAccessor (&RtpH264Client::m_fps),
                   MakeDoubleChecker<double> (1.0, 120.0))
    .AddAttribute ("ConfigInterval", "Seconds between in-band SPS/PPS (STAP-A), like rtph264pay config-interval.",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&RtpH264Client::m_configInterval),
                   MakeDoubleChecker<double> (0.0))
    .AddAttribute ("MaxRtpPayload", "Maximum RTP payload size in bytes.",
                   UintegerValue (1200),
                   MakeUintegerAccessor (&RtpH264Client::m_maxRtpPayload),
                   MakeUintegerChecker<uint32_t> (200, 1400))
    .AddAttribute ("MaxPackets", "Stop after this many RTP packets (0 = until app stop).",
                   UintegerValue (0),
                   MakeUintegerAccessor (&RtpH264Client::m_maxPackets),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("PayloadType", "RTP payload type.",
                   UintegerValue (96),
                   MakeUintegerAccessor (&RtpH264Client::m_payloadType),
                   MakeUintegerChecker<uint8_t> ())
    .AddAttribute ("Ssrc", "RTP synchronization source.",
                   UintegerValue (0x12345678),
                   MakeUintegerAccessor (&RtpH264Client::m_ssrc),
                   MakeUintegerChecker<uint32_t> ());
  return tid;
}

RtpH264Client::RtpH264Client ()
  : m_accessUnitIndex (0),
    m_fps (25.0),
    m_configInterval (1.0),
    m_maxRtpPayload (1200),
    m_maxPackets (0),
    m_sent (0),
    m_seq (0),
    m_timestamp (0),
    m_ssrc (0x12345678),
    m_payloadType (96),
    m_lastConfigSent (Seconds (0))
{
}

RtpH264Client::~RtpH264Client ()
{
}

static bool
IsStartCode3 (const uint8_t *p)
{
  return p[0] == 0 && p[1] == 0 && p[2] == 1;
}

static bool
IsStartCode4 (const uint8_t *p)
{
  return p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1;
}

bool
RtpH264Client::LoadH264File (const std::string &path)
{
  std::ifstream in (path.c_str (), std::ios::binary);
  if (!in.is_open ())
    {
      NS_LOG_ERROR ("Cannot open H.264 file: " << path);
      return false;
    }

  in.seekg (0, std::ios::end);
  const std::streamsize fileSize = in.tellg ();
  in.seekg (0, std::ios::beg);
  if (fileSize <= 0)
    {
      NS_LOG_ERROR ("Empty H.264 file: " << path);
      return false;
    }

  std::vector<uint8_t> raw (static_cast<size_t> (fileSize));
  in.read (reinterpret_cast<char *> (raw.data ()), fileSize);
  if (!in)
    {
      NS_LOG_ERROR ("Failed to read H.264 file: " << path);
      return false;
    }

  m_nals.clear ();
  m_sps.clear ();
  m_pps.clear ();

  size_t i = 0;
  while (i + 3 < raw.size ())
    {
      size_t start = std::string::npos;
      size_t scLen = 0;
      if (i + 3 < raw.size () && IsStartCode3 (&raw[i]))
        {
          start = i;
          scLen = 3;
        }
      else if (i + 4 < raw.size () && IsStartCode4 (&raw[i]))
        {
          start = i;
          scLen = 4;
        }

      if (start == std::string::npos)
        {
          ++i;
          continue;
        }

      size_t nalStart = start + scLen;
      size_t nalEnd = raw.size ();
      for (size_t j = nalStart; j + 3 < raw.size (); ++j)
        {
          if (IsStartCode3 (&raw[j]) || IsStartCode4 (&raw[j]))
            {
              nalEnd = j;
              break;
            }
        }

      if (nalStart >= nalEnd)
        {
          i = nalStart + 1;
          continue;
        }

      NalUnit nal;
      nal.data.assign (raw.begin () + nalStart, raw.begin () + nalEnd);
      nal.type = nal.data.empty () ? 0 : (nal.data[0] & 0x1f);
      nal.vcl = (nal.type == 1 || nal.type == 5);

      if (nal.type == 7)
        {
          m_sps = nal.data;
        }
      else if (nal.type == 8)
        {
          m_pps = nal.data;
        }
      else if (nal.type != 9)
        {
          m_nals.push_back (nal);
        }

      i = nalEnd;
    }

  if (m_nals.empty ())
    {
      NS_LOG_ERROR ("No streamable NAL units found in " << path);
      return false;
    }

  NS_LOG_UNCOND ("Loaded H.264: " << m_nals.size () << " NAL units from " << path
                 << " (SPS=" << m_sps.size () << " B, PPS=" << m_pps.size () << " B)");
  return true;
}

Ptr<Packet>
RtpH264Client::BuildRtpPacket (const uint8_t *payload, uint32_t payloadSize, bool marker)
{
  uint8_t hdr[RTP_HEADER_SIZE];
  hdr[0] = 0x80;
  hdr[1] = static_cast<uint8_t> ((marker ? 0x80 : 0x00) | (m_payloadType & 0x7f));
  hdr[2] = static_cast<uint8_t> (m_seq >> 8);
  hdr[3] = static_cast<uint8_t> (m_seq & 0xff);
  hdr[4] = static_cast<uint8_t> (m_timestamp >> 24);
  hdr[5] = static_cast<uint8_t> (m_timestamp >> 16);
  hdr[6] = static_cast<uint8_t> (m_timestamp >> 8);
  hdr[7] = static_cast<uint8_t> (m_timestamp & 0xff);
  hdr[8] = static_cast<uint8_t> (m_ssrc >> 24);
  hdr[9] = static_cast<uint8_t> (m_ssrc >> 16);
  hdr[10] = static_cast<uint8_t> (m_ssrc >> 8);
  hdr[11] = static_cast<uint8_t> (m_ssrc & 0xff);

  Ptr<Packet> packet = Create<Packet> (hdr, RTP_HEADER_SIZE);
  packet->AddAtEnd (Create<Packet> (payload, payloadSize));
  ++m_seq;
  return packet;
}

void
RtpH264Client::SendSingleNal (const uint8_t *nal, uint32_t nalSize, bool marker)
{
  Ptr<Packet> packet = BuildRtpPacket (nal, nalSize, marker);
  if (m_socket->SendTo (packet, 0, m_peer) >= 0)
    {
      ++m_sent;
    }
}

void
RtpH264Client::SendFuA (const uint8_t *nal, uint32_t nalSize, bool marker)
{
  const uint8_t nalHeader = nal[0];
  const uint8_t nri = nalHeader & 0xe0;
  const uint8_t nalType = nalHeader & 0x1f;
  const uint8_t *payload = nal + 1;
  uint32_t payloadSize = nalSize - 1;
  const uint32_t maxChunk = m_maxRtpPayload - 2;
  uint32_t offset = 0;
  bool first = true;

  while (offset < payloadSize)
    {
      const uint32_t chunk = std::min (maxChunk, payloadSize - offset);
      const bool last = (offset + chunk) >= payloadSize;
      std::vector<uint8_t> fu (2 + chunk);
      fu[0] = nri | 28;
      fu[1] = static_cast<uint8_t> ((first ? 0x80 : 0x00) | (last ? 0x40 : 0x00) | nalType);
      std::memcpy (fu.data () + 2, payload + offset, chunk);

      const bool pktMarker = last && marker;
      Ptr<Packet> packet = BuildRtpPacket (fu.data (), fu.size (), pktMarker);
      if (m_socket->SendTo (packet, 0, m_peer) >= 0)
        {
          ++m_sent;
        }

      offset += chunk;
      first = false;
      if (m_maxPackets > 0 && m_sent >= m_maxPackets)
        {
          return;
        }
    }
}

void
RtpH264Client::SendNal (const NalUnit &nal, bool marker)
{
  if (nal.data.empty ())
    {
      return;
    }

  if (nal.data.size () <= m_maxRtpPayload)
    {
      SendSingleNal (nal.data.data (), nal.data.size (), marker);
    }
  else
    {
      SendFuA (nal.data.data (), nal.data.size (), marker);
    }
}

void
RtpH264Client::SendConfig (void)
{
  if (m_sps.empty () || m_pps.empty ())
    {
      return;
    }

  const uint32_t stapSize = 1 + 2 + m_sps.size () + 2 + m_pps.size ();
  if (stapSize > m_maxRtpPayload)
    {
      NS_LOG_WARN ("SPS/PPS STAP-A too large; skip config burst");
      return;
    }

  std::vector<uint8_t> stap (stapSize);
  size_t o = 0;
  stap[o++] = 0x78;
  stap[o++] = static_cast<uint8_t> ((m_sps.size () >> 8) & 0xff);
  stap[o++] = static_cast<uint8_t> (m_sps.size () & 0xff);
  std::memcpy (stap.data () + o, m_sps.data (), m_sps.size ());
  o += m_sps.size ();
  stap[o++] = static_cast<uint8_t> ((m_pps.size () >> 8) & 0xff);
  stap[o++] = static_cast<uint8_t> (m_pps.size () & 0xff);
  std::memcpy (stap.data () + o, m_pps.data (), m_pps.size ());

  Ptr<Packet> packet = BuildRtpPacket (stap.data (), stap.size (), false);
  if (m_socket->SendTo (packet, 0, m_peer) >= 0)
    {
      ++m_sent;
    }
  m_lastConfigSent = Simulator::Now ();
}

void
RtpH264Client::SendNextAccessUnit (void)
{
  if (m_maxPackets > 0 && m_sent >= m_maxPackets)
    {
      return;
    }

  if (m_configInterval > 0.0
      && (m_lastConfigSent.IsZero ()
          || (Simulator::Now () - m_lastConfigSent).GetSeconds () >= m_configInterval))
    {
      SendConfig ();
    }

  while (m_accessUnitIndex < m_nals.size () && !m_nals[m_accessUnitIndex].vcl)
    {
      SendNal (m_nals[m_accessUnitIndex], false);
      ++m_accessUnitIndex;
      if (m_maxPackets > 0 && m_sent >= m_maxPackets)
        {
          return;
        }
    }

  if (m_accessUnitIndex >= m_nals.size ())
    {
      m_accessUnitIndex = 0;
      m_timestamp = 0;
      ScheduleNextAccessUnit ();
      return;
    }

  SendNal (m_nals[m_accessUnitIndex], true);
  ++m_accessUnitIndex;
  m_timestamp += static_cast<uint32_t> (H264_CLOCK_RATE / m_fps);
  ScheduleNextAccessUnit ();
}

void
RtpH264Client::ScheduleNextAccessUnit (void)
{
  if (m_maxPackets > 0 && m_sent >= m_maxPackets)
    {
      return;
    }
  const Time frameInterval = Seconds (1.0 / m_fps);
  m_sendEvent = Simulator::Schedule (frameInterval, &RtpH264Client::SendNextAccessUnit, this);
}

void
RtpH264Client::ReportStats (void)
{
  NS_LOG_UNCOND ("RtpH264Client node " << GetNode ()->GetId () << ": sent " << m_sent
                 << " RTP packets to "
                 << InetSocketAddress::ConvertFrom (m_peer).GetIpv4 ());
  m_statsEvent = Simulator::Schedule (Seconds (1.0), &RtpH264Client::ReportStats, this);
}

void
RtpH264Client::StartApplication (void)
{
  if (m_peer.IsInvalid ())
    {
      NS_FATAL_ERROR ("RtpH264Client Remote address not set");
    }

  if (!LoadH264File (m_videoFile))
    {
      NS_FATAL_ERROR ("RtpH264Client failed to load video file: " << m_videoFile);
    }

  if (m_socket == 0)
    {
      m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
      if (m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), 0)) == -1)
        {
          NS_FATAL_ERROR ("RtpH264Client failed to bind socket");
        }
      m_socket->SetAllowBroadcast (true);
    }

  m_sent = 0;
  m_seq = 0;
  m_timestamp = 0;
  m_accessUnitIndex = 0;
  m_lastConfigSent = Seconds (0);

  SendConfig ();
  m_sendEvent = Simulator::Schedule (Seconds (0.0), &RtpH264Client::SendNextAccessUnit, this);
  m_statsEvent = Simulator::Schedule (Seconds (1.0), &RtpH264Client::ReportStats, this);

  NS_LOG_UNCOND ("RtpH264Client streaming " << m_videoFile << " -> "
                 << InetSocketAddress::ConvertFrom (m_peer).GetIpv4 ()
                 << ":" << InetSocketAddress::ConvertFrom (m_peer).GetPort ()
                 << " @ " << m_fps << " fps, PT=" << static_cast<uint32_t> (m_payloadType));
}

void
RtpH264Client::StopApplication (void)
{
  Simulator::Cancel (m_sendEvent);
  Simulator::Cancel (m_statsEvent);
  NS_LOG_UNCOND ("RtpH264Client stopped on node " << GetNode ()->GetId ()
                 << "; total RTP packets sent: " << m_sent);
  if (m_socket)
    {
      m_socket->Close ();
      m_socket = 0;
    }
}

} // namespace ns3
