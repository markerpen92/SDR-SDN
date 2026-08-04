/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * mesh-emu-m13.cc
 *
 * Dual-gateway mesh transit for OpenWiFi board:
 * external RTP enters via br0 on node 0, crosses the mesh to node 9,
 * and exits via sdr0 (AP) to 192.168.13.0/24.
 *
 *   192.168.10.x (LAN) --br0--> [Node 0  NAT-1 DNAT]
 *                                    |
 *                               mesh 10.1.1.0/24
 *                                    |
 *   192.168.13.x (WLAN/AP) <--sdr0-- [Node 9  NAT-2 SNAT]
 *
 * NAT-1 (node 0, ingress): dst 192.168.10.111 (ICMP/UDP) -> transit dest on 192.168.13.0/24
 * NAT-2 (node 9, egress):  transit/mesh src -> 192.168.13.111 (+ UDP checksum patch)
 * ICMP echo-reply DNAT on node 9 (same as mesh-emu-openwifi reverse NAT).
 *
 * External RTP (terminal sends video; NS-3 NAT-transits like ping):
 *   OpenWiFi: ./run-mesh-ap.sh
 *   Client (.232 on SSID openwifi @ 192.168.13.188): python3 recv-rtp.py
 *   Sender (.226): python3 send-rtp.py --host 192.168.10.111 --port 5004
 *   Node 0 ingress DNAT 10.111:5004 -> 192.168.13.188, mesh -> node 9 SNAT -> sdr0.
 */

#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <set>
#include <map>

#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/mesh-module.h"
#include "ns3/mobility-module.h"
#include "ns3/mesh-helper.h"
#include "ns3/mesh-point-device.h"
#include "ns3/mesh-wifi-interface-mac.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/wifi-net-device.h"
#include "ns3/qos-txop.h"
// #include "ns3/pyviz.h"
// #include "ns3/visualizer-module.h"

#include "ns3/abort.h"
#include "ns3/fd-net-device-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/icmpv4.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/arp-cache.h"
#include "ns3/ipv4-interface.h"
#include "ns3/udp-l4-protocol.h"
#include "rtp-helper.h"
#include <map>

using namespace ns3;

static const uint32_t INGRESS_NODE = 0;
static const uint32_t EGRESS_NODE = 9;

static void
ConfigureMeshEdca (const NetDeviceContainer &meshDevices)
{
  for (uint32_t i = 0; i < meshDevices.GetN (); ++i)
    {
      Ptr<MeshPointDevice> mp = DynamicCast<MeshPointDevice> (meshDevices.Get (i));
      if (!mp)
        {
          continue;
        }
      const std::vector<Ptr<NetDevice> > ifaces = mp->GetInterfaces ();
      for (uint32_t j = 0; j < ifaces.size (); ++j)
        {
          Ptr<WifiNetDevice> wifi = DynamicCast<WifiNetDevice> (ifaces[j]);
          if (!wifi)
            {
              continue;
            }
          Ptr<MeshWifiInterfaceMac> mac =
            wifi->GetMac ()->GetObject<MeshWifiInterfaceMac> ();
          if (!mac || !mac->GetQosSupported ())
            {
              continue;
            }
          Ptr<QosTxop> vo = mac->GetQosTxop (AC_VO);
          Ptr<QosTxop> vi = mac->GetQosTxop (AC_VI);
          Ptr<QosTxop> be = mac->GetQosTxop (AC_BE);
          Ptr<QosTxop> bk = mac->GetQosTxop (AC_BK);
          if (vo)
            {
              vo->SetAifsn (1);
              vo->SetMinCw (1);
              vo->SetMaxCw (3);
            }
          if (vi)
            {
              vi->SetAifsn (2);
              vi->SetMinCw (3);
              vi->SetMaxCw (7);
            }
          if (be)
            {
              be->SetAifsn (5);
              be->SetMinCw (15);
              be->SetMaxCw (511);
            }
          if (bk)
            {
              bk->SetAifsn (7);
              bk->SetMinCw (15);
              bk->SetMaxCw (1023);
            }
        }
    }
  std::cout << "[QoS] Mesh EDCA tuned (VO/VI favored over BE/BK)\n";
}

static bool
SafeIpv4Frame (Ptr<const Packet> packet)
{
  if (packet->GetSize () < 20)
    {
      return false;
    }
  uint8_t ip[20];
  packet->CopyData (ip, 20);
  const uint8_t version = ip[0] >> 4;
  const uint8_t ihl = static_cast<uint8_t> ((ip[0] & 0x0f) * 4);
  if (version != 4 || ihl < 20)
    {
      return false;
    }
  const uint16_t totalLen =
    static_cast<uint16_t> ((ip[2] << 8) | ip[3]);
  if (totalLen < ihl || totalLen > packet->GetSize ())
    {
      return false;
    }
  return true;
}

static bool
IsIngressRtpTrigger (Ipv4Address dst, Ipv4Address outsideAddr)
{
  return dst == outsideAddr || dst == Ipv4Address ("255.255.255.255");
}

static void
LogEmuUdpPortIfPresent (Ptr<const Packet> packet, uint16_t watchPort, const char *where)
{
  if (!SafeIpv4Frame (packet))
    {
      return;
    }
  const uint32_t len = packet->GetSize ();
  uint8_t buf[64];
  const uint32_t n = std::min (len, static_cast<uint32_t> (sizeof (buf)));
  packet->CopyData (buf, n);
  const uint8_t ihl = static_cast<uint8_t> ((buf[0] & 0x0f) * 4);
  if (ihl < 20 || n < static_cast<uint32_t> (ihl + 4) || buf[9] != UdpL4Protocol::PROT_NUMBER)
    {
      return;
    }
  const uint16_t dport = static_cast<uint16_t> ((buf[ihl + 2] << 8) | buf[ihl + 3]);
  if (dport != watchPort)
    {
      return;
    }
  Ipv4Address src ((buf[12] << 24) | (buf[13] << 16) | (buf[14] << 8) | buf[15]);
  Ipv4Address dst ((buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19]);
  static uint32_t logN = 0;
  if (++logN <= 15 || (logN % 200) == 0)
    {
      std::cout << "[emu " << where << "] UDP :" << watchPort
                << " " << src << " -> " << dst << std::endl;
    }
}

static bool
IsOnInterfaceSubnet (Ipv4Address addr, Ptr<Ipv4Interface> iface)
{
  for (uint32_t i = 0; i < iface->GetNAddresses (); ++i)
    {
      const Ipv4InterfaceAddress ia = iface->GetAddress (i);
      if (addr.CombineMask (ia.GetMask ()) == ia.GetAddress ().CombineMask (ia.GetMask ()))
        {
          return true;
        }
    }
  return false;
}

static std::map<uint32_t, Mac48Address> g_emuPeerMac;

static void
EnsureEmuPeerArp (Ptr<NetDevice> outside, Ipv4Address peer, const char *where)
{
  const auto it = g_emuPeerMac.find (peer.Get ());
  if (it == g_emuPeerMac.end ())
    {
      return;
    }

  Ptr<Node> node = outside->GetNode ();
  Ptr<Ipv4L3Protocol> l3 = node->GetObject<Ipv4L3Protocol> ();
  if (!l3)
    {
      return;
    }

  const int32_t ifIndex = l3->GetInterfaceForDevice (outside);
  if (ifIndex < 0)
    {
      return;
    }

  Ptr<Ipv4Interface> iface = l3->GetInterface (static_cast<uint32_t> (ifIndex));
  if (!iface)
    {
      return;
    }

  Ptr<ArpCache> cache = iface->GetArpCache ();
  if (!cache)
    {
      return;
    }

  ArpCache::Entry *entry = cache->Lookup (peer);
  if (!entry)
    {
      entry = cache->Add (peer);
    }

  if (entry->IsWaitReply ())
    {
      entry->MarkAlive (it->second);
    }
  else
    {
      entry->SetMacAddress (it->second);
      entry->MarkPermanent ();
    }

  static std::set<uint32_t> logged;
  if (logged.insert (peer.Get ()).second)
    {
      std::cout << "[" << where << " ARP ensure] " << peer << " -> "
                << it->second << std::endl;
    }
}

static void
LearnEmuPeerArp (Ptr<Ipv4L3Protocol> ipv4L3,
                 Ptr<NetDevice> device,
                 Ptr<const Packet> packet,
                 const Address &from)
{
  if (!SafeIpv4Frame (packet))
    {
      return;
    }

  uint8_t ip[20];
  packet->CopyData (ip, 20);
  const Ipv4Address src ((ip[12] << 24) | (ip[13] << 16) | (ip[14] << 8) | ip[15]);
  if (src == Ipv4Address::GetAny () || src.IsBroadcast () || src.IsMulticast ())
    {
      return;
    }

  const int32_t ifIndex = ipv4L3->GetInterfaceForDevice (device);
  if (ifIndex < 0)
    {
      return;
    }

  Ptr<Ipv4Interface> iface = ipv4L3->GetInterface (static_cast<uint32_t> (ifIndex));
  if (!iface || !IsOnInterfaceSubnet (src, iface))
    {
      return;
    }

  Ptr<ArpCache> cache = iface->GetArpCache ();
  if (!cache)
    {
      return;
    }

  const Mac48Address peerMac = Mac48Address::ConvertFrom (from);
  const Mac48Address devMac = Mac48Address::ConvertFrom (device->GetAddress ());
  if (peerMac == devMac)
    {
      return;
    }

  ArpCache::Entry *entry = cache->Lookup (src);
  if (!entry)
    {
      entry = cache->Add (src);
    }

  if (entry->IsPermanent () && entry->GetMacAddress () == peerMac)
    {
      return;
    }

  if (entry->IsWaitReply ())
    {
      entry->MarkAlive (peerMac);
    }
  else
    {
      entry->SetMacAddress (peerMac);
      entry->MarkPermanent ();
    }

  g_emuPeerMac[src.Get ()] = peerMac;

  static std::set<uint32_t> logged;
  if (logged.insert (src.Get ()).second)
    {
      std::cout << "[emu ARP learn] " << src << " -> " << peerMac << std::endl;
    }
}

static bool
GatewayEmuPromiscRx (Ptr<Ipv4L3Protocol> ipv4L3,
                     Ptr<NetDevice> device,
                     Ptr<const Packet> packet,
                     uint16_t protocol,
                     const Address &from,
                     const Address &to,
                     NetDevice::PacketType packetType)
{
  LogEmuUdpPortIfPresent (packet, 5004, "promisc");

  if (packetType == NetDevice::NS3_PACKET_OTHERHOST)
    {
      LearnEmuPeerArp (ipv4L3, device, packet, from);

      if (!SafeIpv4Frame (packet))
        {
          return false;
        }
      ipv4L3->Receive (device, packet, protocol, from, to, packetType);
      return true;
    }
  return false;
}

static bool
ReadIcmpEchoIdSeq (Ptr<Packet> packet, uint8_t icmpType, uint16_t &id, uint16_t &seq)
{
  if (packet->GetSize () < 8)
    {
      return false;
    }
  uint8_t buf[8];
  packet->CopyData (buf, 8);
  if (buf[0] != icmpType)
    {
      return false;
    }
  id = static_cast<uint16_t> ((buf[4] << 8) | buf[5]);
  seq = static_cast<uint16_t> ((buf[6] << 8) | buf[7]);
  return true;
}

static Ptr<Packet>
FixIcmpReplyChecksum (Ptr<Packet> packet)
{
  const uint32_t len = packet->GetSize ();
  if (len < 8)
    {
      return packet;
    }
  uint8_t buf[2048];
  if (len > sizeof (buf))
    {
      return packet;
    }
  packet->CopyData (buf, len);
  if (buf[0] != 0)
    {
      return packet;
    }
  buf[2] = 0;
  buf[3] = 0;
  uint32_t sum = 0;
  for (uint32_t i = 0; i + 1 < len; i += 2)
    {
      sum += static_cast<uint32_t> ((buf[i] << 8) | buf[i + 1]);
    }
  if ((len & 1u) != 0u)
    {
      sum += static_cast<uint32_t> (buf[len - 1] << 8);
    }
  while ((sum >> 16) != 0)
    {
      sum = (sum & 0xffff) + (sum >> 16);
    }
  const uint16_t csum = static_cast<uint16_t> (~sum);
  buf[2] = static_cast<uint8_t> (csum >> 8);
  buf[3] = static_cast<uint8_t> (csum & 0xff);
  return Create<Packet> (buf, len);
}

static uint32_t
MakeIcmpMapKey (uint16_t id, uint16_t seq)
{
  return (static_cast<uint32_t> (id) << 16) | seq;
}

static bool
PeekUdpDestPort (Ptr<const Packet> packet, uint16_t &port)
{
  if (packet->GetSize () < 4)
    {
      return false;
    }
  uint8_t buf[4];
  packet->CopyData (buf, 4);
  port = static_cast<uint16_t> ((buf[2] << 8) | buf[3]);
  return true;
}

static Ptr<Packet>
PatchUdpChecksum (Ptr<Packet> packet, Ipv4Address src, Ipv4Address dst)
{
  const uint32_t len = packet->GetSize ();
  if (len < 8 || len > 2048)
    {
      return packet;
    }

  uint8_t buf[2048];
  packet->CopyData (buf, len);

  buf[6] = 0;
  buf[7] = 0;

  const uint16_t udpLen = static_cast<uint16_t> (len);
  uint32_t sum = 0;
  const uint32_t s = src.Get ();
  sum += (s >> 16) & 0xffff;
  sum += (s & 0xffff);
  const uint32_t d = dst.Get ();
  sum += (d >> 16) & 0xffff;
  sum += (d & 0xffff);
  sum += UdpL4Protocol::PROT_NUMBER;
  sum += udpLen;

  for (uint32_t i = 0; i < len; i += 2)
    {
      uint16_t word;
      if (i + 1 < len)
        {
          word = static_cast<uint16_t> ((buf[i] << 8) | buf[i + 1]);
        }
      else
        {
          word = static_cast<uint16_t> (buf[i] << 8);
        }
      sum += word;
    }
  while (sum >> 16)
    {
      sum = (sum & 0xffff) + (sum >> 16);
    }
  uint16_t csum = static_cast<uint16_t> (~sum);
  if (csum == 0)
    {
      csum = 0xffff;
    }

  buf[6] = static_cast<uint8_t> (csum >> 8);
  buf[7] = static_cast<uint8_t> (csum & 0xff);

  return Create<Packet> (buf, len);
}

static bool
IsOutsideEgress (Ptr<Ipv4Route> route, Ptr<NetDevice> outside)
{
  if (!route || !outside || !route->GetOutputDevice ())
    {
      return false;
    }
  return route->GetOutputDevice ()->GetIfIndex () == outside->GetIfIndex ();
}

static const uint8_t H264_NAL_IDR = 5;
static const uint8_t H264_NAL_SLICE = 1;
static const uint8_t H264_NAL_FU_A = 28;

struct H264RtpInspect
{
  bool ok;
  uint16_t seq;
  uint32_t ts;
  uint8_t pt;
  bool marker;
  uint8_t nalType;
  bool idr;
  bool frameEnd;
};

static H264RtpInspect
InspectH264RtpUdp (Ptr<const Packet> udpPacket)
{
  H264RtpInspect r {};
  r.ok = false;
  const uint32_t len = udpPacket->GetSize ();
  if (len < 8 + 12)
    {
      return r;
    }

  uint8_t buf[2048];
  const uint32_t n = std::min (len, static_cast<uint32_t> (sizeof (buf)));
  udpPacket->CopyData (buf, n);

  const uint32_t udpPayload = n - 8;
  const uint8_t *rtp = buf + 8;
  if ((rtp[0] >> 6) != 2)
    {
      return r;
    }

  const uint8_t cc = rtp[0] & 0x0f;
  const bool ext = (rtp[0] & 0x10) != 0;
  r.marker = (rtp[1] & 0x80) != 0;
  r.pt = rtp[1] & 0x7f;
  r.seq = static_cast<uint16_t> ((rtp[2] << 8) | rtp[3]);
  r.ts = (rtp[4] << 24) | (rtp[5] << 16) | (rtp[6] << 8) | rtp[7];

  uint32_t hdrLen = 12 + cc * 4;
  if (ext)
    {
      if (udpPayload < hdrLen + 4)
        {
          return r;
        }
      const uint16_t extWords = static_cast<uint16_t> ((rtp[hdrLen + 2] << 8) | rtp[hdrLen + 3]);
      hdrLen += 4 + extWords * 4;
    }
  if (udpPayload <= hdrLen)
    {
      return r;
    }

  const uint8_t *payload = rtp + hdrLen;
  const uint32_t payloadLen = udpPayload - hdrLen;
  r.nalType = payload[0] & 0x1f;

  if (r.nalType == H264_NAL_IDR)
    {
      r.idr = true;
    }
  else if (r.nalType == H264_NAL_FU_A && payloadLen >= 2)
    {
      const uint8_t fuType = payload[1] & 0x1f;
      const bool start = (payload[1] & 0x80) != 0;
      if (fuType == H264_NAL_IDR && start)
        {
          r.idr = true;
        }
      if (fuType == H264_NAL_IDR || fuType == H264_NAL_SLICE)
        {
          r.frameEnd = r.marker;
        }
    }

  if (r.nalType == H264_NAL_IDR || r.nalType == H264_NAL_SLICE)
    {
      r.frameEnd = r.marker;
    }

  r.ok = true;
  return r;
}

/**
 * RTP/H.264 FPS observer (marker + slice/IDR) at a gateway hop.
 */
class RtpFpsStats
{
public:
  struct Sample
  {
    double timeS;
    double fps;
    double throughputKbps;
    uint32_t frames;
    uint32_t idrFrames;
    uint32_t rtpPackets;
    uint32_t bytes;
  };

  RtpFpsStats ()
    : m_enabled (false),
      m_windowFrames (0),
      m_windowIdr (0),
      m_windowPkts (0),
      m_windowBytes (0),
      m_totalFrames (0),
      m_totalIdr (0),
      m_totalPkts (0),
      m_totalBytes (0),
      m_seqGaps (0),
      m_haveSeq (false),
      m_lastSeq (0),
      m_windowStart (Seconds (0)),
      m_nextSampleTime (1.0),
      m_label ("RTP")
  {
  }

  void Enable (bool on)
  {
    m_enabled = on;
  }

  void SetLabel (const std::string &label)
  {
    m_label = label;
  }

  void SetCsvPath (const std::string &path)
  {
    m_csvPath = path;
  }

  void SetSampleInterval (double seconds)
  {
    m_nextSampleTime = seconds;
  }

  void Observe (Ptr<const Packet> udpPacket)
  {
    if (!m_enabled)
      {
        return;
      }

    const H264RtpInspect info = InspectH264RtpUdp (udpPacket);
    if (!info.ok)
      {
        return;
      }

    EnsureTimer ();
    ++m_windowPkts;
    ++m_totalPkts;
    m_windowBytes += udpPacket->GetSize ();
    m_totalBytes += udpPacket->GetSize ();

    if (m_haveSeq)
      {
        const uint16_t diff = static_cast<uint16_t> (info.seq - m_lastSeq);
        if (diff > 1 && diff < 0x8000)
          {
            m_seqGaps += diff - 1;
          }
      }
    m_lastSeq = info.seq;
    m_haveSeq = true;

    if (info.idr)
      {
        ++m_windowIdr;
        ++m_totalIdr;
        static uint32_t idrLog = 0;
        if (++idrLog <= 30 || (idrLog % 50) == 0)
          {
            std::cout << "[" << m_label << " I-frame] seq=" << info.seq
                      << " ts=" << info.ts << " pt=" << static_cast<uint32_t> (info.pt)
                      << " nal=" << static_cast<uint32_t> (info.nalType) << std::endl;
          }
      }

    if (info.frameEnd)
      {
        ++m_windowFrames;
        ++m_totalFrames;
      }
  }

  void StopAndWriteCsv ()
  {
    if (!m_enabled || m_csvPath.empty ())
      {
        return;
      }
    if (!m_reportEvent.IsExpired ())
      {
        Simulator::Cancel (m_reportEvent);
        m_reportEvent = EventId ();
      }
    RecordWindow ();
    WriteCsv ();
  }

private:
  void EnsureTimer ()
  {
    if (!m_reportEvent.IsExpired ())
      {
        return;
      }
    m_windowStart = Simulator::Now ();
    m_reportEvent = Simulator::Schedule (Seconds (m_nextSampleTime), &RtpFpsStats::Report, this);
  }

  void RecordWindow ()
  {
    if (m_windowPkts == 0)
      {
        return;
      }
    const double elapsed = std::max (1e-6, (Simulator::Now () - m_windowStart).GetSeconds ());
    Sample s;
    s.timeS = Simulator::Now ().GetSeconds ();
    s.fps = m_windowFrames / elapsed;
    s.throughputKbps = (m_windowBytes * 8.0) / elapsed / 1000.0;
    s.frames = m_windowFrames;
    s.idrFrames = m_windowIdr;
    s.rtpPackets = m_windowPkts;
    s.bytes = m_windowBytes;
    m_samples.push_back (s);
  }

  void Report ()
  {
    RecordWindow ();

    if (!m_samples.empty ())
      {
        const Sample &last = m_samples.back ();
        std::cout << "[" << m_label << " FPS] t=" << last.timeS << "s FPS=" << last.fps
                  << " (frames=" << last.frames << "/s)"
                  << " thr=" << last.throughputKbps << " kbps"
                  << " I-frame=" << last.idrFrames << "/s (total IDR=" << m_totalIdr << ")"
                  << " rtp_pkts=" << last.rtpPackets << "/s (total=" << m_totalPkts << ")"
                  << " seq_gaps=" << m_seqGaps << std::endl;
      }

    m_windowFrames = 0;
    m_windowIdr = 0;
    m_windowPkts = 0;
    m_windowBytes = 0;
    m_windowStart = Simulator::Now ();
    m_reportEvent = Simulator::Schedule (Seconds (m_nextSampleTime), &RtpFpsStats::Report, this);
  }

  void WriteCsv () const
  {
    if (m_samples.empty ())
      {
        std::cerr << "[" << m_label << " FPS] no samples; CSV not written\n";
        return;
      }
    std::ofstream out (m_csvPath.c_str ());
    if (!out.is_open ())
      {
        std::cerr << "[" << m_label << " FPS] failed to open " << m_csvPath << " for write\n";
        return;
      }
    out << "time_s,fps,throughput_kbps,frames,idr_frames,rtp_packets,bytes\n";
    for (std::vector<Sample>::const_iterator it = m_samples.begin (); it != m_samples.end (); ++it)
      {
        out << it->timeS << "," << it->fps << "," << it->throughputKbps << ","
            << it->frames << "," << it->idrFrames << "," << it->rtpPackets << ","
            << it->bytes << "\n";
      }
    out.close ();
    std::cout << "[" << m_label << " FPS] wrote " << m_samples.size () << " rows to "
              << m_csvPath << std::endl;
  }

  bool m_enabled;
  uint32_t m_windowFrames;
  uint32_t m_windowIdr;
  uint32_t m_windowPkts;
  uint32_t m_windowBytes;
  uint32_t m_totalFrames;
  uint32_t m_totalIdr;
  uint32_t m_totalPkts;
  uint32_t m_totalBytes;
  uint32_t m_seqGaps;
  bool m_haveSeq;
  uint16_t m_lastSeq;
  Time m_windowStart;
  EventId m_reportEvent;
  double m_nextSampleTime;
  std::string m_csvPath;
  std::string m_label;
  std::vector<Sample> m_samples;
};

static RtpFpsStats g_node0RtpStats;
static RtpFpsStats g_node9RtpStats;

static void
LogRtpTransit (const char *gateway,
               const char *direction,
               Ipv4Address src,
               Ipv4Address dst,
               Ptr<const Packet> udpPacket,
               uint16_t rtpPort)
{
  uint16_t dport = 0;
  if (!PeekUdpDestPort (udpPacket, dport) || dport != rtpPort)
    {
      return;
    }
  if (udpPacket->GetSize () < 20)
    {
      return;
    }
  uint8_t buf[20];
  udpPacket->CopyData (buf, 20);
  const uint8_t *rtp = buf + 8;
  const uint16_t seq = static_cast<uint16_t> ((rtp[2] << 8) | rtp[3]);
  const uint32_t ts = (rtp[4] << 24) | (rtp[5] << 16) | (rtp[6] << 8) | rtp[7];
  const uint32_t ssrc = (rtp[8] << 24) | (rtp[9] << 16) | (rtp[10] << 8) | rtp[11];
  const uint8_t pt = rtp[1] & 0x7f;

  static uint32_t logCount = 0;
  if (++logCount <= 10 || (logCount % 200) == 0)
    {
      std::cout << "[" << gateway << " RTP " << direction << "] "
                << src << " -> " << dst
                << " seq=" << seq << " ts=" << ts
                << " ssrc=0x" << std::hex << ssrc << std::dec
                << " pt=" << static_cast<uint32_t> (pt)
                << std::endl;
    }
}

/**
 * Border gateway NAT: optional ingress DNAT on outside Rx, optional egress SNAT on outside Tx.
 */
class MeshBorderNatRouting : public Ipv4RoutingProtocol
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::MeshBorderNatRouting")
      .SetParent<Ipv4RoutingProtocol> ()
      .SetGroupName ("Internet")
      .AddConstructor<MeshBorderNatRouting> ();
    return tid;
  }

  MeshBorderNatRouting ()
    : m_outsideAddr ("0.0.0.0"),
      m_insideNet ("10.1.1.0"),
      m_insideMask ("255.255.255.0"),
      m_ingressDnatDest ("0.0.0.0"),
      m_ingressRtpDnatDest ("0.0.0.0"),
      m_rtpPort (5004),
      m_doIngressDnat (false),
      m_doIngressRtpDnat (false),
      m_doEgressSnat (false),
      m_snatTransit (false),
      m_hasTransitNet (false),
      m_transitNet ("0.0.0.0"),
      m_transitMask ("255.255.255.0"),
      m_gatewayLabel ("gw"),
      m_enableQos (false),
      m_rtpTos (0xB8),
      m_icmpTos (0x08),
      m_defaultTos (0x00)
  {
  }

  void SetStaticRouting (Ptr<Ipv4StaticRouting> routing)
  {
    m_routing = routing;
  }

  void SetOutsideDevice (Ptr<NetDevice> device)
  {
    m_outside = device;
  }

  void SetOutsideAddress (Ipv4Address addr)
  {
    m_outsideAddr = addr;
  }

  void SetInsideNetwork (Ipv4Address net, Ipv4Mask mask)
  {
    m_insideNet = net;
    m_insideMask = mask;
  }

  void SetIngressDnatDest (Ipv4Address addr)
  {
    m_ingressDnatDest = addr;
    m_doIngressDnat = true;
  }

  void SetIngressRtpDnatDest (Ipv4Address addr)
  {
    m_ingressRtpDnatDest = addr;
    m_doIngressRtpDnat = true;
    m_doIngressDnat = true;
  }

  void SetRtpPort (uint16_t port)
  {
    m_rtpPort = port;
  }

  void EnableEgressSnat (bool enable)
  {
    m_doEgressSnat = enable;
  }

  void EnableTransitSnat (bool enable)
  {
    m_snatTransit = enable;
  }

  void SetTransitNetwork (Ipv4Address net, Ipv4Mask mask)
  {
    m_transitNet = net;
    m_transitMask = mask;
    m_hasTransitNet = true;
  }

  void SetGatewayLabel (const std::string &label)
  {
    m_gatewayLabel = label;
  }

  void SetIngressQos (bool enable, uint8_t rtpTos, uint8_t icmpTos, uint8_t defaultTos)
  {
    m_enableQos = enable;
    m_rtpTos = rtpTos;
    m_icmpTos = icmpTos;
    m_defaultTos = defaultTos;
  }

  Ptr<Ipv4Route> RouteOutput (Ptr<Packet> p, const Ipv4Header &header,
                              Ptr<NetDevice> oif, Socket::SocketErrno &sockerr) override
  {
    return m_routing->RouteOutput (p, header, oif, sockerr);
  }

  bool RouteInput (Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                   UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                   LocalDeliverCallback lcb, ErrorCallback ecb) override
  {
    m_forwardCb = ucb;
    UnicastForwardCallback natUcb =
      MakeCallback (&MeshBorderNatRouting::NatUnicastForward, this);

    Ipv4Header hdr = header;
    Ptr<Packet> packet = p->Copy ();

    if (m_outside && idev == m_outside && hdr.GetFragmentOffset () == 0)
      {
        const Ipv4Address origDst = hdr.GetDestination ();
        const bool rtpIngressTrigger =
          (hdr.GetProtocol () == UdpL4Protocol::PROT_NUMBER && m_doIngressRtpDnat
           && IsIngressRtpTrigger (origDst, m_outsideAddr));

        if (m_doIngressDnat && origDst == m_outsideAddr)
          {
            if (hdr.GetProtocol () == 1)
              {
                uint16_t id = 0;
                uint16_t seq = 0;
                if (ReadIcmpEchoIdSeq (packet, Icmpv4Header::ICMPV4_ECHO, id, seq))
                  {
                    std::cout << "[" << m_gatewayLabel << " ICMP ingress DNAT] "
                              << hdr.GetSource () << " -> " << origDst
                              << " id=" << id << " seq=" << seq
                              << " => " << m_ingressDnatDest << std::endl;
                    m_icmpIngressReplySnat.insert (MakeIcmpMapKey (id, seq));
                    hdr.SetDestination (m_ingressDnatDest);
                    if (Node::ChecksumEnabled ())
                      {
                        hdr.EnableChecksum ();
                      }
                  }
              }
          }
        if (rtpIngressTrigger)
          {
            uint16_t dport = 0;
            if (PeekUdpDestPort (packet, dport) && dport == m_rtpPort)
              {
                g_node0RtpStats.Observe (packet);
                LogRtpTransit (m_gatewayLabel.c_str (), "ingress",
                               hdr.GetSource (), hdr.GetDestination (), packet, m_rtpPort);
                hdr.SetDestination (m_ingressRtpDnatDest);
                packet = PatchUdpChecksum (packet, hdr.GetSource (), hdr.GetDestination ());
                if (Node::ChecksumEnabled ())
                  {
                    hdr.EnableChecksum ();
                  }
              }
          }
        else if (m_doIngressDnat && hdr.GetDestination () == m_outsideAddr
                 && hdr.GetProtocol () == UdpL4Protocol::PROT_NUMBER)
          {
            uint16_t dport = 0;
            if (PeekUdpDestPort (packet, dport) && dport == m_rtpPort)
              {
                static uint32_t localRtpLog = 0;
                if (++localRtpLog <= 20 || (localRtpLog % 100) == 0)
                  {
                    std::cout << "[" << m_gatewayLabel << " RTP local] "
                              << hdr.GetSource () << " -> " << hdr.GetDestination ()
                              << ":" << dport << " (deliver to RtpServer)\n";
                  }
              }
          }
        else if (m_hasTransitNet && IsTransitDest (origDst))
          {
            if (hdr.GetProtocol () == 1)
              {
                uint16_t id = 0;
                uint16_t seq = 0;
                if (ReadIcmpEchoIdSeq (packet, Icmpv4Header::ICMPV4_ECHO, id, seq))
                  {
                    static uint32_t icmpTransitLog = 0;
                    if (++icmpTransitLog <= 30 || (icmpTransitLog % 50) == 0)
                      {
                        std::cout << "[" << m_gatewayLabel << " ICMP transit] "
                                  << hdr.GetSource () << " -> " << origDst
                                  << " id=" << id << " seq=" << seq
                                  << " (forward into mesh)\n";
                      }
                  }
              }
          }
        else if (hdr.GetDestination () == m_outsideAddr && hdr.GetProtocol () == 1
                 && hdr.GetFragmentOffset () == 0)
          {
                uint16_t id = 0;
                uint16_t seq = 0;
                if (ReadIcmpEchoIdSeq (packet, Icmpv4Header::ICMPV4_ECHO_REPLY, id, seq))
                  {
                    const uint32_t key = MakeIcmpMapKey (id, seq);
                    auto it = m_icmpEchoMap.find (key);
                    if (it != m_icmpEchoMap.end ())
                      {
                        std::cout << "[" << m_gatewayLabel << " ICMP reply DNAT] "
                                  << hdr.GetSource () << " -> " << m_outsideAddr
                                  << " id=" << id << " seq=" << seq
                                  << " => " << it->second << std::endl;
                        hdr.SetDestination (it->second);
                        m_icmpEchoMap.erase (it);
                    if (Node::ChecksumEnabled ())
                      {
                        hdr.EnableChecksum ();
                      }
                  }
              }
          }

        ApplyIngressQos (hdr, packet);
      }

    return m_routing->RouteInput (packet, hdr, idev, natUcb, mcb, lcb, ecb);
  }

  void NotifyInterfaceUp (uint32_t interface) override
  {
    m_routing->NotifyInterfaceUp (interface);
  }

  void NotifyInterfaceDown (uint32_t interface) override
  {
    m_routing->NotifyInterfaceDown (interface);
  }

  void NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address) override
  {
    m_routing->NotifyAddAddress (interface, address);
  }

  void NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address) override
  {
    m_routing->NotifyRemoveAddress (interface, address);
  }

  void SetIpv4 (Ptr<Ipv4> ipv4) override
  {
  }

  void PrintRoutingTable (Ptr<OutputStreamWrapper> stream, Time::Unit unit) const override
  {
    m_routing->PrintRoutingTable (stream, unit);
  }

private:
  void ApplyIngressQos (Ipv4Header &hdr, Ptr<const Packet> packet) const
  {
    if (!m_enableQos)
      {
        return;
      }

    uint8_t tos = m_defaultTos;
    const char *traffic = "else";
    const uint8_t proto = hdr.GetProtocol ();

    if (proto == UdpL4Protocol::PROT_NUMBER)
      {
        uint16_t dport = 0;
        if (PeekUdpDestPort (packet, dport) && dport == m_rtpPort)
          {
            tos = m_rtpTos;
            traffic = "RTP";
          }
      }
    else if (proto == 1)
      {
        tos = m_icmpTos;
        traffic = "ICMP";
      }

    hdr.SetTos (tos);
    if (Node::ChecksumEnabled ())
      {
        hdr.EnableChecksum ();
      }

    static uint32_t qosLog = 0;
    if (++qosLog <= 10)
      {
        std::cout << "[" << m_gatewayLabel << " QoS] " << traffic
                  << " TOS=0x" << std::hex << static_cast<uint32_t> (tos) << std::dec << std::endl;
      }
  }

  bool IsInside (Ipv4Address addr) const
  {
    return addr.CombineMask (m_insideMask) == m_insideNet;
  }

  bool IsTransitDest (Ipv4Address addr) const
  {
    return addr.CombineMask (m_transitMask) == m_transitNet;
  }

  bool ShouldEgressSnat (const Ipv4Header &hdr) const
  {
    if (hdr.GetSource () == m_outsideAddr)
      {
        return false;
      }
    if (IsInside (hdr.GetSource ()))
      {
        return true;
      }
    if (m_snatTransit && !IsInside (hdr.GetDestination ()))
      {
        return true;
      }
    return false;
  }

  void NatUnicastForward (Ptr<Ipv4Route> route, Ptr<const Packet> p,
                          const Ipv4Header &header)
  {
    Ptr<Packet> packet = p->Copy ();
    Ipv4Header hdr = header;

    if (m_doEgressSnat && IsOutsideEgress (route, m_outside)
        && hdr.GetFragmentOffset () == 0)
      {
        if (ShouldEgressSnat (hdr))
          {
            if (hdr.GetProtocol () == 1)
              {
                uint16_t id = 0;
                uint16_t seq = 0;
                if (ReadIcmpEchoIdSeq (packet, Icmpv4Header::ICMPV4_ECHO, id, seq))
                  {
                    m_icmpEchoMap[MakeIcmpMapKey (id, seq)] = hdr.GetSource ();
                  }
              }
            hdr.SetSource (m_outsideAddr);
            route->SetSource (m_outsideAddr);
          }

        if (hdr.GetProtocol () == UdpL4Protocol::PROT_NUMBER)
          {
            uint16_t dport = 0;
            if (PeekUdpDestPort (packet, dport) && dport == m_rtpPort)
              {
                g_node9RtpStats.Observe (packet);
              }
            LogRtpTransit (m_gatewayLabel.c_str (), "egress",
                           hdr.GetSource (), hdr.GetDestination (), packet, m_rtpPort);
            packet = PatchUdpChecksum (packet, hdr.GetSource (), hdr.GetDestination ());
            static uint32_t patchLog = 0;
            if (++patchLog <= 3)
              {
                std::cout << "[" << m_gatewayLabel << "] UDP checksum patched for "
                          << hdr.GetSource () << " -> " << hdr.GetDestination ()
                          << std::endl;
              }
          }

        if (Node::ChecksumEnabled ())
          {
            hdr.EnableChecksum ();
          }
      }

    if (IsOutsideEgress (route, m_outside) && hdr.GetFragmentOffset () == 0
        && hdr.GetProtocol () == 1)
      {
        uint16_t id = 0;
        uint16_t seq = 0;
        if (ReadIcmpEchoIdSeq (packet, Icmpv4Header::ICMPV4_ECHO_REPLY, id, seq))
          {
            const uint32_t key = MakeIcmpMapKey (id, seq);
            if (m_doIngressDnat && !m_doEgressSnat
                && m_icmpIngressReplySnat.erase (key) > 0)
              {
                std::cout << "[" << m_gatewayLabel << " ICMP reply ingress SNAT] "
                          << hdr.GetSource () << " -> " << m_outsideAddr
                          << " id=" << id << " seq=" << seq << std::endl;
                hdr.SetSource (m_outsideAddr);
                route->SetSource (m_outsideAddr);
                if (Node::ChecksumEnabled ())
                  {
                    hdr.EnableChecksum ();
                  }
              }
            static uint32_t replyEgressLog = 0;
            if (++replyEgressLog <= 30 || (replyEgressLog % 50) == 0)
              {
                std::cout << "[" << m_gatewayLabel << " ICMP reply egress] "
                          << hdr.GetSource () << " -> " << hdr.GetDestination ()
                          << " id=" << id << " seq=" << seq << std::endl;
              }
            packet = FixIcmpReplyChecksum (packet);
            if (Node::ChecksumEnabled ())
              {
                hdr.EnableChecksum ();
              }
            EnsureEmuPeerArp (m_outside, hdr.GetDestination (), m_gatewayLabel.c_str ());
          }
      }

    if (IsOutsideEgress (route, m_outside) && hdr.GetFragmentOffset () == 0)
      {
        EnsureEmuPeerArp (m_outside, hdr.GetDestination (), m_gatewayLabel.c_str ());
      }

    m_forwardCb (route, packet, hdr);
  }

  Ptr<Ipv4StaticRouting> m_routing;
  UnicastForwardCallback m_forwardCb;
  Ptr<NetDevice> m_outside;
  Ipv4Address m_outsideAddr;
  Ipv4Address m_insideNet;
  Ipv4Mask m_insideMask;
  Ipv4Address m_ingressDnatDest;
  Ipv4Address m_ingressRtpDnatDest;
  uint16_t m_rtpPort;
  bool m_doIngressDnat;
  bool m_doIngressRtpDnat;
  bool m_doEgressSnat;
  bool m_snatTransit;
  bool m_hasTransitNet;
  Ipv4Address m_transitNet;
  Ipv4Mask m_transitMask;
  std::string m_gatewayLabel;
  std::map<uint32_t, Ipv4Address> m_icmpEchoMap;
  std::set<uint32_t> m_icmpIngressReplySnat;
  bool m_enableQos;
  uint8_t m_rtpTos;
  uint8_t m_icmpTos;
  uint8_t m_defaultTos;
};

NS_OBJECT_ENSURE_REGISTERED (MeshBorderNatRouting);

NS_LOG_COMPONENT_DEFINE ("MeshEmuM13");

static void
PingRtt (std::string context, Time rtt)
{
  NS_LOG_UNCOND ("Received Response with RTT = " << rtt << " || " << context);
}

static void
PrintHostEmuSetup (const std::string &ifName,
                   const std::string &vip,
                   Ptr<NetDevice> dev,
                   const char *role)
{
  Mac48Address mac = Mac48Address::ConvertFrom (dev->GetAddress ());
  std::ostringstream macStr;
  macStr << mac;
  std::cout << "\n=== Host setup: " << role << " (" << ifName << " / " << vip << ") ===\n"
            << "  ip link set " << ifName << " promisc on   # may need privileges\n"
            << "  ip neigh replace " << vip << " lladdr " << macStr.str ()
            << " dev " << ifName << " nud permanent   # same-PC gst/ping only\n"
            << "  NS-3 learns LAN host MAC from promisc RX ([emu ARP learn], same /24 only)\n"
            << "  If ping sees tcpdump reply but 0% success: check dual-homed host notes at startup\n"
            << "  Or send gst from another host on 192.168.10.0/24 (no static neigh needed)\n";
}

static void
InstallEmuOnNode (NodeContainer &nodes,
                  uint32_t nodeId,
                  const std::string &ifName,
                  const std::string &addr,
                  const std::string &gateway,
                  Ptr<NetDevice> &outDev)
{
  EmuFdNetDeviceHelper emu;
  emu.SetDeviceName (ifName);
  NetDeviceContainer emuDevices = emu.Install (nodes.Get (nodeId));
  Ptr<NetDevice> device = emuDevices.Get (0);
  outDev = device;
  device->SetAttribute ("Address", Mac48AddressValue (Mac48Address::Allocate ()));

  Ptr<Ipv4> ipv4 = nodes.Get (nodeId)->GetObject<Ipv4> ();
  uint32_t ifIndex = ipv4->AddInterface (device);
  Ipv4InterfaceAddress ifAddr = Ipv4InterfaceAddress (addr.c_str (), "255.255.255.0");
  ipv4->AddAddress (ifIndex, ifAddr);
  ipv4->SetMetric (ifIndex, 1);
  ipv4->SetUp (ifIndex);

  Ipv4StaticRoutingHelper routingHelper;
  Ptr<Ipv4StaticRouting> rt = routingHelper.GetStaticRouting (ipv4);
  rt->SetDefaultRoute (Ipv4Address (gateway.c_str ()), ifIndex);
}

class MeshM13Test
{
public:
  MeshM13Test ();
  void Configure (int argc, char **argv);
  int Run ();

private:
  int       m_xSize;
  int       m_ySize;
  double    m_step;
  double    m_randomStart;
  double    m_totalTime;
  uint32_t  m_nIfaces;
  bool      m_chan;
  bool      m_pcap;
  bool      m_pcapIp;
  bool      m_ascii;
  std::string m_pcapPrefix;
  std::string m_stack;
  std::string m_root;
  std::string m_ingressDevice;
  std::string m_ingressAddr;
  std::string m_ingressGateway;
  std::string m_egressDevice;
  std::string m_egressAddr;
  std::string m_egressGateway;
  std::string m_rtpRemote;
  uint16_t  m_rtpPort;
  uint32_t  m_ingressNode;
  uint32_t  m_egressNode;
  bool      m_enablePing;
  bool      m_rtpLocalParse;
  bool      m_rtpFpsStats;
  std::string m_rtpNode0StatsCsv;
  std::string m_rtpNode9StatsCsv;
  double    m_rtpStatsInterval;
  std::string m_pingRemote;
  uint32_t  m_pingNode;
  bool      m_enableQos;
  uint32_t  m_rtpTos;
  uint32_t  m_icmpTos;
  uint32_t  m_defaultTos;

  NodeContainer nodes;
  NetDeviceContainer meshDevices;
  Ipv4InterfaceContainer interfaces;
  MeshHelper mesh;
  YansWifiPhyHelper m_wifiPhy;
  Ptr<NetDevice> m_ingressEmuDev;
  Ptr<NetDevice> m_egressEmuDev;

  void CreateNodes ();
  void InstallInternetStack ();
  void EnablePcapTraces ();
  void InstallApplication ();
  void Report ();
};

MeshM13Test::MeshM13Test ()
  : m_xSize (4),
    m_ySize (3),
    m_step (5.0), //25.0
    m_randomStart (0.1),
    m_totalTime (100.0),
    m_nIfaces (1),
    m_chan (true),
    m_pcap (false),
    m_pcapIp (true),
    m_ascii (false),
    m_pcapPrefix ("mesh-m13"),
    m_stack ("ns3::Dot11sStack"),
    m_root ("ff:ff:ff:ff:ff:ff"),
    m_ingressDevice ("br0"),
    m_ingressAddr ("192.168.10.111"),
    m_ingressGateway ("192.168.10.1"),
    m_egressDevice ("sdr0"),
    m_egressAddr ("192.168.13.111"),
    m_egressGateway ("192.168.13.1"),
    m_rtpRemote ("192.168.13.188"),
    m_rtpPort (5004),
    m_ingressNode (INGRESS_NODE),
    m_egressNode (EGRESS_NODE),
    m_enablePing (false),
    m_rtpLocalParse (false),
    m_rtpFpsStats (true),
    m_rtpNode0StatsCsv ("node0-rtp-stats.csv"),
    m_rtpNode9StatsCsv ("node9-rtp-stats.csv"),
    m_rtpStatsInterval (1.0),
    m_pingRemote ("192.168.13.188"),
    m_pingNode (8),
    m_enableQos (false),
    m_rtpTos (0xB8),
    m_icmpTos (0x08),
    m_defaultTos (0x00),
    m_ingressEmuDev (0),
    m_egressEmuDev (0)
{
}

void
MeshM13Test::Configure (int argc, char *argv[])
{
  CommandLine cmd (__FILE__);
  cmd.AddValue ("x-size", "Number of nodes in a row grid", m_xSize);
  cmd.AddValue ("y-size", "Number of rows in a grid", m_ySize);
  cmd.AddValue ("step", "Size of edge in our grid (meters)", m_step);
  cmd.AddValue ("start", "Maximum random start delay for beacon jitter (sec)", m_randomStart);
  cmd.AddValue ("time", "Simulation time (sec)", m_totalTime);
  cmd.AddValue ("interfaces", "Number of radio interfaces used by each mesh point", m_nIfaces);
  cmd.AddValue ("channels", "Use different frequency channels for different interfaces", m_chan);
  cmd.AddValue ("pcap", "Enable PCAP traces", m_pcap);
  cmd.AddValue ("pcap-prefix", "File name prefix for PCAP output", m_pcapPrefix);
  cmd.AddValue ("pcap-ip", "Also record IPv4-level PCAP", m_pcapIp);
  cmd.AddValue ("ascii", "Enable Ascii traces on interfaces", m_ascii);
  cmd.AddValue ("stack", "Mesh stack installer", m_stack);
  cmd.AddValue ("root", "Mac address of root mesh point in HWMP", m_root);
  cmd.AddValue ("ingress-device", "Ingress FdNetDevice NIC (node 0)", m_ingressDevice);
  cmd.AddValue ("ingress-addr", "Ingress gateway IP on 192.168.10.0/24", m_ingressAddr);
  cmd.AddValue ("ingress-gateway", "Ingress LAN default gateway", m_ingressGateway);
  cmd.AddValue ("egress-device", "Egress FdNetDevice NIC (node 9)", m_egressDevice);
  cmd.AddValue ("egress-addr", "Egress gateway IP on 192.168.13.0/24", m_egressAddr);
  cmd.AddValue ("egress-gateway", "Egress LAN default gateway", m_egressGateway);
  cmd.AddValue ("rtp-remote", "Final RTP destination on 192.168.13.0/24", m_rtpRemote);
  cmd.AddValue ("rtp-port", "RTP/UDP port", m_rtpPort);
  cmd.AddValue ("ingress-node", "Mesh node with br0 ingress", m_ingressNode);
  cmd.AddValue ("egress-node", "Mesh node with sdr0 egress", m_egressNode);
  cmd.AddValue ("enable-ping", "Enable V4Ping from a mesh node (like openwifi)", m_enablePing);
  cmd.AddValue ("rtp-local-parse", "Use RtpServer socket on ingress (default: NAT transit like ping)", m_rtpLocalParse);
  cmd.AddValue ("rtp-fps-stats", "FPS stats on node 0 ingress and node 9 egress", m_rtpFpsStats);
  cmd.AddValue ("rtp-node0-stats-csv", "CSV for node 0 ingress FPS", m_rtpNode0StatsCsv);
  cmd.AddValue ("rtp-node9-stats-csv", "CSV for node 9 egress FPS", m_rtpNode9StatsCsv);
  cmd.AddValue ("rtp-stats-interval", "Seconds between FPS CSV samples", m_rtpStatsInterval);
  cmd.AddValue ("enable-qos", "Mark IP TOS for RTP/ICMP/other at gateway ingress", m_enableQos);
  cmd.AddValue ("rtp-tos", "IP TOS for RTP (UDP dst port=rtp-port), default 0xB8 EF", m_rtpTos);
  cmd.AddValue ("icmp-tos", "IP TOS for ICMP, default 0x08 CS1", m_icmpTos);
  cmd.AddValue ("default-tos", "IP TOS for other traffic, default 0x00", m_defaultTos);
  cmd.AddValue ("ping-remote", "V4Ping / ingress DNAT destination on 192.168.13.0/24", m_pingRemote);
  cmd.AddValue ("ping-node", "Mesh node for V4Ping", m_pingNode);

  cmd.Parse (argc, argv);

  NS_ABORT_MSG_IF (m_egressNode >= static_cast<uint32_t> (m_xSize * m_ySize),
                   "egress-node out of range; increase x-size/y-size (need >= 10 nodes for node 9)");

  // {PyViz v;}
  // if (m_ascii)
  //   {
  //     PacketMetadata::Enable ();
  //   }
}

void
MeshM13Test::CreateNodes ()
{
  nodes.Create (static_cast<uint32_t> (m_xSize * m_ySize));

  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  m_wifiPhy.SetChannel (wifiChannel.Create ());

  mesh = MeshHelper::Default ();
  if (!Mac48Address (m_root.c_str ()).IsBroadcast ())
    {
      mesh.SetStackInstaller (m_stack, "Root", Mac48AddressValue (Mac48Address (m_root.c_str ())));
    }
  else
    {
      mesh.SetStackInstaller (m_stack);
    }
  if (m_chan)
    {
      mesh.SetSpreadInterfaceChannels (MeshHelper::SPREAD_CHANNELS);
    }
  else
    {
      mesh.SetSpreadInterfaceChannels (MeshHelper::ZERO_CHANNEL);
    }
  mesh.SetMacType ("RandomStart", TimeValue (Seconds (m_randomStart)));
  mesh.SetNumberOfInterfaces (m_nIfaces);
  meshDevices = mesh.Install (m_wifiPhy, nodes);
  mesh.AssignStreams (meshDevices, 0);

  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  InternetStackHelper internetStackHelper;
  internetStackHelper.Install (nodes);

  InstallEmuOnNode (nodes, m_ingressNode, m_ingressDevice, m_ingressAddr,
                    m_ingressGateway, m_ingressEmuDev);
  InstallEmuOnNode (nodes, m_egressNode, m_egressDevice, m_egressAddr,
                    m_egressGateway, m_egressEmuDev);

  PrintHostEmuSetup (m_ingressDevice, m_ingressAddr, m_ingressEmuDev, "ingress (RTP + ping)");
  PrintHostEmuSetup (m_egressDevice, m_egressAddr, m_egressEmuDev, "egress (SNAT to 1.x LAN)");

  MobilityHelper mobility;
  mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                   "MinX", DoubleValue (0.0),
                                   "MinY", DoubleValue (0.0),
                                   "DeltaX", DoubleValue (m_step),
                                   "DeltaY", DoubleValue (m_step),
                                   "GridWidth", UintegerValue (m_xSize),
                                   "LayoutType", StringValue ("RowFirst"));
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (nodes);

  if (m_ascii)
    {
      AsciiTraceHelper ascii;
      m_wifiPhy.EnableAsciiAll (ascii.CreateFileStream ("mesh-m13.tr"));
    }
}

void
MeshM13Test::EnablePcapTraces ()
{
  if (!m_pcap)
    {
      return;
    }

  for (uint32_t i = 0; i < meshDevices.GetN (); ++i)
    {
      Ptr<MeshPointDevice> mp = DynamicCast<MeshPointDevice> (meshDevices.Get (i));
      if (!mp)
        {
          continue;
        }
      const uint32_t nodeId = mp->GetNode ()->GetId ();
      const std::vector<Ptr<NetDevice> > ifaces = mp->GetInterfaces ();
      for (uint32_t j = 0; j < ifaces.size (); ++j)
        {
          Ptr<WifiNetDevice> wifi = DynamicCast<WifiNetDevice> (ifaces[j]);
          if (!wifi)
            {
              continue;
            }
          std::ostringstream name;
          name << m_pcapPrefix << "-wifi-node" << nodeId << "-if" << j;
          m_wifiPhy.EnablePcap (name.str (), wifi);
        }
    }

  if (m_pcapIp)
    {
      InternetStackHelper stackHelper;
      stackHelper.EnablePcapIpv4All (m_pcapPrefix + "-ipv4");
    }

  EmuFdNetDeviceHelper emu;
  if (m_ingressEmuDev)
    {
      emu.EnablePcap (m_pcapPrefix + "-ingress-emu", m_ingressEmuDev, true);
    }
  if (m_egressEmuDev)
    {
      emu.EnablePcap (m_pcapPrefix + "-egress-emu", m_egressEmuDev, true);
    }
}

static void
SetupBorderGateway (Ptr<Node> gwNode,
                    Ptr<NetDevice> emuDev,
                    const std::string &outsideAddrStr,
                    const std::string &outsideGw,
                    bool ingressDnat,
                    Ipv4Address ingressIcmpDnatDest,
                    Ipv4Address ingressRtpDnatDest,
                    bool egressSnat,
                    const std::string &label,
                    uint16_t rtpPort,
                    bool enableQos,
                    uint8_t rtpTos,
                    uint8_t icmpTos,
                    uint8_t defaultTos)
{
  Ptr<Ipv4> ipv4Gw = gwNode->GetObject<Ipv4> ();
  ipv4Gw->SetAttribute ("IpForward", BooleanValue (true));

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> gwStatic = ipv4RoutingHelper.GetStaticRouting (ipv4Gw);
  int32_t emuIf = ipv4Gw->GetInterfaceForDevice (emuDev);
  NS_ASSERT (emuIf >= 0);
  gwStatic->SetDefaultRoute (Ipv4Address (outsideGw.c_str ()), emuIf);

  Ipv4Address outsideAddr (outsideAddrStr.c_str ());

  Ptr<MeshBorderNatRouting> natRouting = CreateObject<MeshBorderNatRouting> ();
  natRouting->SetStaticRouting (gwStatic);
  natRouting->SetOutsideDevice (emuDev);
  natRouting->SetOutsideAddress (outsideAddr);
  natRouting->SetInsideNetwork (Ipv4Address ("10.1.1.0"), Ipv4Mask ("255.255.255.0"));
  natRouting->SetRtpPort (rtpPort);
  natRouting->SetGatewayLabel (label);
  natRouting->SetIngressQos (enableQos, rtpTos, icmpTos, defaultTos);
  if (ingressDnat)
    {
      natRouting->SetIngressDnatDest (ingressIcmpDnatDest);
      if (ingressRtpDnatDest != Ipv4Address::GetAny ())
        {
          natRouting->SetIngressRtpDnatDest (ingressRtpDnatDest);
        }
      natRouting->SetTransitNetwork (Ipv4Address ("192.168.13.0"), Ipv4Mask ("255.255.255.0"));
    }
  natRouting->EnableEgressSnat (egressSnat);
  natRouting->EnableTransitSnat (egressSnat && !ingressDnat);
  ipv4Gw->SetRoutingProtocol (natRouting);

  Ptr<Ipv4L3Protocol> ipv4L3 = gwNode->GetObject<Ipv4L3Protocol> ();
  emuDev->SetPromiscReceiveCallback (MakeBoundCallback (&GatewayEmuPromiscRx, ipv4L3));
}

void
MeshM13Test::InstallInternetStack ()
{
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  interfaces = address.Assign (meshDevices);

  Ipv4Address ingressMeshAddr = interfaces.GetAddress (m_ingressNode);
  Ipv4Address egressMeshAddr = interfaces.GetAddress (m_egressNode);
  Ipv4Address transitDest (m_pingRemote.c_str ());
  Ipv4Address rtpDnatDest = m_rtpLocalParse ? Ipv4Address::GetAny ()
                                            : Ipv4Address (m_rtpRemote.c_str ());

  Ipv4StaticRoutingHelper ipv4RoutingHelper;

  Ptr<Ipv4> ipv4Ingress = nodes.Get (m_ingressNode)->GetObject<Ipv4> ();
  int32_t ingressMeshIf = ipv4Ingress->GetInterfaceForDevice (meshDevices.Get (m_ingressNode));
  NS_ASSERT (ingressMeshIf >= 0);
  Ptr<Ipv4StaticRouting> rtIngress = ipv4RoutingHelper.GetStaticRouting (ipv4Ingress);
  rtIngress->AddNetworkRouteTo (Ipv4Address ("192.168.13.0"), Ipv4Mask ("255.255.255.0"),
                                egressMeshAddr, ingressMeshIf);

  Ptr<Ipv4> ipv4Egress = nodes.Get (m_egressNode)->GetObject<Ipv4> ();
  int32_t egressMeshIf = ipv4Egress->GetInterfaceForDevice (meshDevices.Get (m_egressNode));
  NS_ASSERT (egressMeshIf >= 0);
  Ptr<Ipv4StaticRouting> rtEgress = ipv4RoutingHelper.GetStaticRouting (ipv4Egress);
  rtEgress->AddNetworkRouteTo (Ipv4Address ("192.168.10.0"), Ipv4Mask ("255.255.255.0"),
                               ingressMeshAddr, egressMeshIf);

  SetupBorderGateway (nodes.Get (m_ingressNode), m_ingressEmuDev,
                      m_ingressAddr, m_ingressGateway, true, transitDest,
                      rtpDnatDest,
                      false, "node0-br0", m_rtpPort,
                      m_enableQos,
                      static_cast<uint8_t> (m_rtpTos),
                      static_cast<uint8_t> (m_icmpTos),
                      static_cast<uint8_t> (m_defaultTos));

  const bool fpsStats = m_rtpFpsStats && !m_rtpLocalParse;
  g_node0RtpStats.Enable (fpsStats);
  g_node9RtpStats.Enable (fpsStats);
  if (fpsStats)
    {
      g_node0RtpStats.SetLabel ("node0");
      g_node9RtpStats.SetLabel ("node9");
      g_node0RtpStats.SetCsvPath (m_rtpNode0StatsCsv);
      g_node9RtpStats.SetCsvPath (m_rtpNode9StatsCsv);
      g_node0RtpStats.SetSampleInterval (m_rtpStatsInterval);
      g_node9RtpStats.SetSampleInterval (m_rtpStatsInterval);
    }

  SetupBorderGateway (nodes.Get (m_egressNode), m_egressEmuDev,
                      m_egressAddr, m_egressGateway, false,
                      Ipv4Address::GetAny (), Ipv4Address::GetAny (),
                      true, "node9-sdr0", m_rtpPort,
                      m_enableQos,
                      static_cast<uint8_t> (m_rtpTos),
                      static_cast<uint8_t> (m_icmpTos),
                      static_cast<uint8_t> (m_defaultTos));

  if (m_enableQos)
    {
      ConfigureMeshEdca (meshDevices);
    }

  for (uint32_t n = 0; n < nodes.GetN (); ++n)
    {
      if (n == m_ingressNode || n == m_egressNode)
        {
          continue;
        }
      Ptr<Ipv4> ipv4 = nodes.Get (n)->GetObject<Ipv4> ();
      int32_t meshIf = ipv4->GetInterfaceForDevice (meshDevices.Get (n));
      NS_ASSERT (meshIf >= 0);
      Ptr<Ipv4StaticRouting> rt = ipv4RoutingHelper.GetStaticRouting (ipv4);
      rt->AddNetworkRouteTo (Ipv4Address ("192.168.13.0"), Ipv4Mask ("255.255.255.0"),
                             egressMeshAddr, meshIf);
      rt->AddNetworkRouteTo (Ipv4Address ("192.168.10.0"), Ipv4Mask ("255.255.255.0"),
                             ingressMeshAddr, meshIf);
    }

  std::cout << "\n=== External terminal ping (transit through mesh) ===\n"
            << "Host setup: see emu MAC / ip neigh commands printed above.\n"
            << "  sudo ./EnableM13.sh   # on the NS-3 gateway PC only\n"
            << "\n"
            << "  *** Same-PC limitation (FdNetDevice / AF_PACKET hairpin) ***\n"
            << "  Do NOT use this PC's own 192.168.10.x address to ping through br0 emu.\n"
            << "  tcpdump will show correct echo replies, but the local kernel will not\n"
            << "  deliver them to ping (packets are TX-injected, not received as INPUT).\n"
            << "  Use either:\n"
            << "    A) Another host on 192.168.10.0/24 only (route 1.0/24 via " << m_ingressAddr << ")\n"
            << "    B) In-sim V4Ping: --enable-ping=1 --enable-ping=0 for external\n"
            << "\n"
            << "  External host (NOT the NS-3 gateway PC):\n"
            << "    ip neigh replace " << m_ingressAddr << " lladdr <emu-mac> dev <iface>\n"
            << "    ip route add 192.168.13.0/24 via " << m_ingressAddr << " dev <iface>\n"
            << "    ping " << m_pingRemote << "\n"
            << "  Path: " << m_ingressDevice << " (node " << m_ingressNode
            << ") -> mesh -> node " << m_egressNode << " / " << m_egressDevice
            << " (SNAT " << m_egressAddr << ") -> " << m_pingRemote << "\n"
            << "Optional mesh-internal test: --enable-ping=1 --ping-node=8\n\n";
}

void
MeshM13Test::InstallApplication ()
{
  if (m_rtpLocalParse)
    {
      Address rtpLocal =
        InetSocketAddress (Ipv4Address::GetAny (), m_rtpPort);
      Address rtpForward =
        InetSocketAddress (Ipv4Address (m_rtpRemote.c_str ()), m_rtpPort);
      RtpServerHelper rtpIngress (rtpLocal);
      ApplicationContainer rtpApps = rtpIngress.Install (nodes.Get (m_ingressNode));
      Ptr<RtpServer> rtpSrv = rtpApps.Get (0)->GetObject<RtpServer> ();
      rtpSrv->SetAttribute ("ForwardRemote", AddressValue (rtpForward));
      rtpApps.Start (Seconds (0.5));
      rtpApps.Stop (Seconds (m_totalTime + 1.0));
      std::cout << "RtpServer ingress on node " << m_ingressNode << " @ "
                << m_ingressAddr << ":" << m_rtpPort
                << " (local parse + relay -> " << m_rtpRemote << ")\n";
    }
  else
    {
      std::cout << "RTP ingress: NAT transit on node " << m_ingressNode
                << " (" << m_ingressAddr << ":" << m_rtpPort
                << " -> " << m_rtpRemote << ":" << m_rtpPort
                << " via mesh, same path as ping)\n";
      if (m_rtpFpsStats)
        {
          std::cout << "RTP FPS stats: node " << m_ingressNode << " ingress -> "
                    << m_rtpNode0StatsCsv << ", node " << m_egressNode
                    << " egress -> " << m_rtpNode9StatsCsv << "\n";
        }
      if (m_enableQos)
        {
          std::cout << "Ingress QoS: RTP TOS=0x" << std::hex << m_rtpTos
                    << " ICMP TOS=0x" << m_icmpTos
                    << " else TOS=0x" << m_defaultTos << std::dec
                    << " (gateway RouteInput + mesh EDCA)\n";
        }
      else if (m_enablePing)
        {
          std::cout << "Note: --enable-ping without --enable-qos=1 leaves ICMP at TOS=0 "
                    << "(same WiFi AC as RTP on mesh)\n";
        }
    }

  if (m_enablePing)
    {
      NS_ABORT_MSG_IF (m_pingNode >= nodes.GetN (), "ping-node out of range");
      NS_LOG_INFO ("Install V4Ping on node " << m_pingNode << " -> " << m_pingRemote);
      Ptr<V4Ping> pingapp = CreateObject<V4Ping> ();
      pingapp->SetAttribute ("Remote", Ipv4AddressValue (m_pingRemote.c_str ()));
      pingapp->SetAttribute ("Verbose", BooleanValue (true));
      if (m_enableQos)
        {
          pingapp->SetAttribute ("IpTos", UintegerValue (m_icmpTos));
        }
      nodes.Get (m_pingNode)->AddApplication (pingapp);
      pingapp->SetStartTime (Seconds (2.0));
      pingapp->SetStopTime (Seconds (m_totalTime + 1.5));

      std::ostringstream pingName;
      pingName << "pingapp-" << m_pingNode;
      Names::Add (pingName.str (), pingapp);
      Config::Connect ("/Names/" + pingName.str () + "/Rtt", MakeCallback (&PingRtt));
      std::cout << "V4Ping: mesh node " << m_pingNode << " -> " << m_pingRemote
                << " (via node " << m_egressNode << " egress SNAT)";
      if (m_enableQos)
        {
          std::cout << " IpTos=0x" << std::hex << m_icmpTos << std::dec;
        }
      std::cout << "\n";
    }

  std::cout << "External RTP (4.x terminal, NS-3 does NOT generate video):\n"
            << "  gst-launch ... udpsink host=" << m_ingressAddr << " port=" << m_rtpPort << "\n"
            << "  same PC (no ip neigh): udpsink host=192.168.10.255 port=" << m_rtpPort << "\n"
            << "  -> ingress DNAT -> " << m_rtpRemote << ":" << m_rtpPort
            << " -> mesh -> node " << m_egressNode << " / " << m_egressDevice << "\n"
            << "  Watch for [node0-br0 RTP ingress] and [emu promisc] UDP :5004 logs\n";
}

int
MeshM13Test::Run ()
{
  CreateNodes ();
  InstallInternetStack ();
  EnablePcapTraces ();
  InstallApplication ();
  Simulator::Schedule (Seconds (m_totalTime), &MeshM13Test::Report, this);
  Simulator::Stop (Seconds (m_totalTime + 2));
  Simulator::Run ();
  g_node0RtpStats.StopAndWriteCsv ();
  g_node9RtpStats.StopAndWriteCsv ();
  Simulator::Destroy ();
  return 0;
}

void
MeshM13Test::Report ()
{
  unsigned n (0);
  for (NetDeviceContainer::Iterator i = meshDevices.Begin (); i != meshDevices.End (); ++i, ++n)
    {
      std::ostringstream os;
      os << "mp-report-m13-" << n << ".xml";
      std::ofstream of;
      of.open (os.str ().c_str ());
      if (of.is_open ())
        {
          mesh.Report (*i, of);
          of.close ();
        }
    }
}

int
main (int argc, char *argv[])
{
  MeshM13Test t;
  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  t.Configure (argc, argv);
  return t.Run ();
}
