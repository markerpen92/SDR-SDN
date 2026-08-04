/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008,2009 IITP RAS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Kirill Andreev <andreev@iitp.ru>
 *
 *
 * By default this script creates m_xSize * m_ySize square grid topology with
 * IEEE802.11s stack installed at each node with peering management
 * and HWMP protocol.
 * The side of the square cell is defined by m_step parameter.
 * When topology is created, UDP ping is installed to opposite corners
 * by diagonals. packet size of the UDP ping and interval between two
 * successive packets is configurable.
 * 
 *  m_xSize * step
 *  |<--------->|
 *   step
 *  |<--->|
 *  * --- * --- * <---Ping sink  _
 *  | \   |   / |                ^
 *  |   \ | /   |                |
 *  * --- * --- * m_ySize * step |
 *  |   / | \   |                |
 *  | /   |   \ |                |
 *  * --- * --- *                _
 *  ^ Ping source
 *
 * By varying m_xSize and m_ySize, one can configure the route that is used.
 * When the inter-nodal distance is small, the source can reach the sink
 * directly.  When the inter-nodal distance is intermediate, the route
 * selected is diagonal (two hop).  When the inter-nodal distance is a bit
 * larger, the diagonals cannot be used and a four-hop route is selected.
 * When the distance is a bit larger, the packets will fail to reach even the
 * adjacent nodes.
 *
 * As of ns-3.36 release, with default configuration (mesh uses Wi-Fi 802.11a
 * standard and the ArfWifiManager rate control by default), the maximum
 * range is roughly 50m.  The default step size in this program is set to 50m,
 * so any mesh packets in the above diagram depiction will not be received
 * successfully on the diagonal hops between two nodes but only on the
 * horizontal and vertical hops.  If the step size is reduced to 35m, then
 * the shortest path will be on the diagonal hops.  If the step size is reduced
 * to 17m or less, then the source will be able to reach the sink directly
 * without any mesh hops (for the default 3x3 mesh depicted above). 
 *
 * The position allocator will lay out the nodes in the following order
 * (corresponding to Node ID and to the diagram above):
 *
 * 6 - 7 - 8
 * |   |   | 
 * 3 - 4 - 5
 * |   |   | 
 * 0 - 1 - 2
 *
 *  See also MeshTest::Configure to read more about configurable
 *  parameters.
 */

 #include <iostream>
 #include <sstream>
 #include <fstream>
 
 // for mesh
 #include "ns3/core-module.h"
 #include "ns3/internet-module.h"
 #include "ns3/network-module.h"
 #include "ns3/applications-module.h"
 #include "ns3/mesh-module.h"
 #include "ns3/mobility-module.h"
#include "ns3/mesh-helper.h"
#include "ns3/mesh-point-device.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/wifi-net-device.h"
// #include "ns3/pyviz.h"
// #include "ns3/visualizer-module.h"
 
 // for fdnetdevice with internet stack
 #include "ns3/abort.h"
 #include "ns3/fd-net-device-module.h"
 #include "ns3/internet-apps-module.h"
 #include "ns3/ipv4-static-routing-helper.h"
 #include "ns3/ipv4-list-routing-helper.h"
#include "ns3/icmpv4.h"
#include "ns3/ipv4-l3-protocol.h"
#include "rtp-helper.h"
#include <map>

using namespace ns3;

/**
 * Replies to 192.168.13.111 often arrive with the host NIC MAC (OTHERHOST).
 * FdNetDevice drops those unless we lift them into the IPv4 stack here.
 */
static bool
GatewayEmuPromiscRx (Ptr<Ipv4L3Protocol> ipv4L3,
                     Ptr<NetDevice> device,
                     Ptr<const Packet> packet,
                     uint16_t protocol,
                     const Address &from,
                     const Address &to,
                     NetDevice::PacketType packetType)
{
  if (packetType == NetDevice::NS3_PACKET_OTHERHOST)
    {
      ipv4L3->Receive (device, packet, protocol, from, to, packetType);
      return true;
    }
  return false;
}

static bool
ReadIcmpEchoSeq (Ptr<Packet> packet, uint8_t icmpType, uint16_t &seq)
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
  seq = buf[6] | (buf[7] << 8);
  return true;
}

/**
 * Wrap Ipv4StaticRouting on the gateway: SNAT mesh sources to the Emu address
 * when forwarding out the outside device; DNAT ICMP echo replies back.
 */
class MeshGatewayNatRouting : public Ipv4RoutingProtocol
 {
 public:
   static TypeId GetTypeId (void)
   {
     static TypeId tid = TypeId ("ns3::MeshGatewayNatRouting")
       .SetParent<Ipv4RoutingProtocol> ()
       .SetGroupName ("Internet")
       .AddConstructor<MeshGatewayNatRouting> ();
     return tid;
   }

   MeshGatewayNatRouting ()
     : m_outsideAddr ("192.168.13.111"),
       m_insideNet ("10.1.1.0"),
       m_insideMask ("255.255.255.0")
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
    UnicastForwardCallback snatUcb =
      MakeCallback (&MeshGatewayNatRouting::SnatUnicastForward, this);

    Ipv4Header hdr = header;
    Ptr<Packet> packet = p->Copy ();
    if (m_outside && idev == m_outside && hdr.GetDestination () == m_outsideAddr
        && hdr.GetProtocol () == 1)
      {
        uint16_t seq = 0;
        if (ReadIcmpEchoSeq (packet, Icmpv4Header::ICMPV4_ECHO_REPLY, seq))
          {
            auto it = m_icmpEchoMap.find (seq);
            if (it != m_icmpEchoMap.end ())
              {
                hdr.SetDestination (it->second);
                m_icmpEchoMap.erase (it);
              }
          }
      }

    return m_routing->RouteInput (packet, hdr, idev, snatUcb, mcb, lcb, ecb);
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
    // m_routing was already bound during InternetStackHelper::Install.
    // Calling SetIpv4 again triggers assert (m_ipv4 == 0) in Ipv4StaticRouting.
  }

   void PrintRoutingTable (Ptr<OutputStreamWrapper> stream, Time::Unit unit) const override
   {
     m_routing->PrintRoutingTable (stream, unit);
   }

 private:
   bool IsInside (Ipv4Address addr) const
   {
     return addr.CombineMask (m_insideMask) == m_insideNet;
   }

   void SnatUnicastForward (Ptr<Ipv4Route> route, Ptr<const Packet> p,
                            const Ipv4Header &header)
   {
     Ptr<Packet> packet = p->Copy ();
     Ipv4Header hdr = header;

     if (route && m_outside && route->GetOutputDevice () == m_outside)
       {
        if (IsInside (hdr.GetSource ()) && hdr.GetSource () != m_outsideAddr)
          {
            if (hdr.GetProtocol () == 1)
              {
                uint16_t seq = 0;
                if (ReadIcmpEchoSeq (packet, Icmpv4Header::ICMPV4_ECHO, seq))
                  {
                    m_icmpEchoMap[seq] = hdr.GetSource ();
                  }
              }
            hdr.SetSource (m_outsideAddr);
            route->SetSource (m_outsideAddr);
          }
       }

     m_forwardCb (route, packet, hdr);
   }

   Ptr<Ipv4StaticRouting> m_routing;
   UnicastForwardCallback m_forwardCb;
   Ptr<NetDevice> m_outside;
   Ipv4Address m_outsideAddr;
   Ipv4Address m_insideNet;
   Ipv4Mask m_insideMask;
   std::map<uint16_t, Ipv4Address> m_icmpEchoMap;
 };

 NS_OBJECT_ENSURE_REGISTERED (MeshGatewayNatRouting);

 NS_LOG_COMPONENT_DEFINE ("MeshExample");
 
 // Declaring these variables outside of main() for use in trace sinks
 uint32_t g_udpTxCount = 0;
 uint32_t g_udpRxCount = 0;
 
 static bool g_verbose = true;
 
 void
 TxTrace (Ptr<const Packet> p)
 {
   NS_LOG_DEBUG ("Sent " << p->GetSize () << " bytes");  
   g_udpTxCount++;
 }
 
 void
 RxTrace (Ptr<const Packet> p)
 {
   NS_LOG_DEBUG ("Received " << p->GetSize () << " bytes");  
   g_udpRxCount++;
 }

 static void
 PingRtt (std::string context, Time rtt)
 {
   NS_LOG_UNCOND ("Received Response with RTT = " << rtt << " || " << context);
 }
 
 
 /**
  * \ingroup mesh
  * \brief MeshTest class
  */
 class MeshTest
 {
 public:
   /// Init test
   MeshTest ();
   /**
    * Configure test from command line arguments
    *
    * \param argc command line argument count
    * \param argv command line arguments
    */
   void Configure (int argc, char ** argv);
   /**
    * Run test
    * \returns the test status
    */
   int Run ();
 private:
   int       m_xSize; ///< X size
   int       m_ySize; ///< Y size
   double    m_step; ///< step
   double    m_randomStart; ///< random start
   double    m_totalTime; ///< total time
   double    m_packetInterval; ///< packet interval
   uint16_t  m_packetSize; ///< packet size
   uint32_t  m_nIfaces; ///< number interfaces
   bool      m_chan; ///< channel
  bool      m_pcap; ///< PCAP
  bool      m_pcapIp; ///< IPv4-level PCAP (ICMP/UDP visible in Wireshark)
  bool      m_ascii; ///< ASCII
  std::string m_pcapPrefix; ///< PCAP file name prefix
  std::string m_stack; ///< stack
  std::string m_root; ///< root
  std::string m_rtpRemote; ///< RTP/UDP destination (Python host on LAN)
  uint16_t  m_rtpPort; ///< RTP/UDP destination port
  uint32_t  m_rtpNode; ///< Mesh node that sends RTP
  double    m_rtpInterval; ///< Seconds between RTP packets
  uint32_t  m_rtpPayloadSize; ///< RTP payload bytes (excl. header)
  bool      m_enablePing; ///< Optional V4Ping (off by default)
  std::string m_pingRemote; ///< V4Ping target (e.g. 8.8.8.8)
  uint32_t  m_pingNode; ///< Node for optional V4Ping
  /// List of network nodes
  NodeContainer nodes;
  /// List of all mesh point devices
  NetDeviceContainer meshDevices;
  /// Addresses of interfaces:
  Ipv4InterfaceContainer interfaces;
  /// MeshHelper. Report is not static methods
  MeshHelper mesh;
  YansWifiPhyHelper m_wifiPhy; ///< kept for PCAP on mesh devices
  Ptr<NetDevice> m_emuDev; ///< gateway Emu device (node 0)
   
 private:
  /// Create nodes and setup their mobility
  void CreateNodes ();
  /// Install internet m_stack on nodes
  void InstallInternetStack ();
  /// Enable Wi-Fi / IPv4 / Emu PCAP traces for Wireshark
  void EnablePcapTraces ();
  /// Install applications
  void InstallApplication ();
  /// Print mesh devices diagnostics
  void Report ();
 };
 MeshTest::MeshTest () :
   m_xSize (3),
   m_ySize (3),
   m_step (50.0),
   m_randomStart (0.1),
   m_totalTime (100.0),
   m_packetInterval (1),
   m_packetSize (1024),
   m_nIfaces (1),
   m_chan (true),
  m_pcap (false),
  m_pcapIp (true),
  m_ascii (false),
  m_pcapPrefix ("mesh-gateway"),
  m_stack ("ns3::Dot11sStack"),
  m_root ("ff:ff:ff:ff:ff:ff"),
  m_rtpRemote ("192.168.13.12"),
  m_rtpPort (5004),
  m_rtpNode (8),
  m_rtpInterval (5.0),
  m_rtpPayloadSize (1024),
  m_enablePing (true),
  m_pingRemote ("192.168.13.12"),
  m_pingNode (8),
  m_emuDev (0)
{
}
 void
 MeshTest::Configure (int argc, char *argv[])
 {
   CommandLine cmd (__FILE__);
   cmd.AddValue ("x-size", "Number of nodes in a row grid", m_xSize);
   cmd.AddValue ("y-size", "Number of rows in a grid", m_ySize);
   cmd.AddValue ("step",   "Size of edge in our grid (meters)", m_step);
   // Avoid starting all mesh nodes at the same time (beacons may collide)
   cmd.AddValue ("start",  "Maximum random start delay for beacon jitter (sec)", m_randomStart);
   cmd.AddValue ("time",  "Simulation time (sec)", m_totalTime);
   cmd.AddValue ("packet-interval",  "Interval between packets in UDP ping (sec)", m_packetInterval);
   cmd.AddValue ("packet-size",  "Size of packets in UDP ping (bytes)", m_packetSize);
   cmd.AddValue ("interfaces", "Number of radio interfaces used by each mesh point", m_nIfaces);
   cmd.AddValue ("channels",   "Use different frequency channels for different interfaces", m_chan);
  cmd.AddValue ("pcap",   "Enable PCAP traces (mesh Wi-Fi, IPv4, gateway Emu)", m_pcap);
  cmd.AddValue ("pcap-prefix", "File name prefix for PCAP output", m_pcapPrefix);
  cmd.AddValue ("pcap-ip", "Also record IPv4-level PCAP (recommended for Wireshark)", m_pcapIp);
  cmd.AddValue ("ascii",   "Enable Ascii traces on interfaces", m_ascii);
   cmd.AddValue ("stack",  "Type of protocol stack. ns3::Dot11sStack by default", m_stack);
   cmd.AddValue ("root", "Mac address of root mesh point in HWMP", m_root);
   cmd.AddValue ("verbose", "Print trace information if true", g_verbose);
   cmd.AddValue ("rtp-remote", "RTP/UDP destination IP (Python receiver on LAN)", m_rtpRemote);
   cmd.AddValue ("rtp-port", "RTP/UDP destination port", m_rtpPort);
   cmd.AddValue ("rtp-node", "Mesh node id that sends RTP toward rtp-remote", m_rtpNode);
   cmd.AddValue ("rtp-interval", "Seconds between RTP packets", m_rtpInterval);
   cmd.AddValue ("rtp-payload-size", "RTP payload size in bytes (excl. 12-byte header)", m_rtpPayloadSize);
   cmd.AddValue ("enable-ping", "Enable V4Ping (independent from RTP; default off)", m_enablePing);
   cmd.AddValue ("ping-remote", "V4Ping destination (use 8.8.8.8 for Internet test)", m_pingRemote);
   cmd.AddValue ("ping-node", "Node id for optional V4Ping", m_pingNode);

   cmd.Parse (argc, argv);

   // {PyViz v;}
   NS_LOG_DEBUG ("Grid:" << m_xSize << "*" << m_ySize);
   NS_LOG_DEBUG ("Simulation time: " << m_totalTime << " s");
   if (m_ascii)
     {
       PacketMetadata::Enable ();
     }
 }
 void
 MeshTest::CreateNodes ()
 { 
   /*
    * Create m_ySize*m_xSize stations to form a grid topology
    */
   nodes.Create (m_ySize*m_xSize);
   // Configure YansWifiChannel
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  m_wifiPhy.SetChannel (wifiChannel.Create ());
   /*
    * Create mesh helper and set stack installer to it
    * Stack installer creates all needed protocols and install them to
    * mesh point device
    */
   mesh = MeshHelper::Default ();
   if (!Mac48Address (m_root.c_str ()).IsBroadcast ())
     {
       mesh.SetStackInstaller (m_stack, "Root", Mac48AddressValue (Mac48Address (m_root.c_str ())));
     }
   else
     {
       //If root is not set, we do not use "Root" attribute, because it
       //is specified only for 11s
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
   // Set number of interfaces - default is single-interface mesh point
   mesh.SetNumberOfInterfaces (m_nIfaces);
   // Install protocols and return container if MeshPointDevices
   meshDevices = mesh.Install (m_wifiPhy, nodes);
   // AssignStreams can optionally be used to control random variable streams
   mesh.AssignStreams (meshDevices, 0);
   // Setup mobility - static grid topology

   GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
   GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

   std::string mode = "ConfigureLocal";
   std::string EmuName = "sdr0";
   EmuFdNetDeviceHelper emu;
   emu.SetDeviceName (EmuName);
   NetDeviceContainer EmuDevices = emu.Install (nodes.Get (0)); 
  Ptr<NetDevice> device = EmuDevices.Get (0);
  m_emuDev = device;
  device->SetAttribute ("Address", Mac48AddressValue (Mac48Address::Allocate ()));

   InternetStackHelper internetStackHelper;
   internetStackHelper.Install (nodes);
   Ptr<Ipv4> ipv4 = nodes.Get (0)->GetObject<Ipv4> ();
   uint32_t interface = ipv4->AddInterface (device);
   Ipv4InterfaceAddress address = Ipv4InterfaceAddress ("192.168.13.111", "255.255.255.0");
   ipv4->AddAddress (interface, address);
   ipv4->SetMetric (interface, 1);
   ipv4->SetUp (interface);

   std::string localGateway ("192.168.13.1");
   Ipv4Address gateway (localGateway.c_str ());
   Ipv4StaticRoutingHelper ipv4RoutingHelper;
   Ptr<Ipv4StaticRouting> staticRouting = ipv4RoutingHelper.GetStaticRouting (ipv4);
   staticRouting->SetDefaultRoute (gateway, interface);

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
      m_wifiPhy.EnableAsciiAll (ascii.CreateFileStream ("mesh.tr"));
    }
}
void
MeshTest::EnablePcapTraces ()
{
  if (!m_pcap)
    {
      return;
    }

  // MeshPointDevice is not a WifiNetDevice; PCAP must attach to each underlying
  // WifiPhy via MonitorSnifferRx/Tx (captures transit/forward frames, not only
  // frames destined to the mesh STA address).
  for (uint32_t i = 0; i < meshDevices.GetN (); ++i)
    {
      Ptr<MeshPointDevice> mp = DynamicCast<MeshPointDevice> (meshDevices.Get (i));
      if (!mp)
        {
          NS_LOG_WARN ("mesh device " << i << " is not a MeshPointDevice; skip Wi-Fi PCAP");
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

  if (m_emuDev)
    {
      EmuFdNetDeviceHelper emu;
      emu.EnablePcap (m_pcapPrefix + "-emu", m_emuDev, true);
    }

  std::cout << "PCAP tracing enabled (prefix \"" << m_pcapPrefix << "\"):\n"
            << "  Mesh Wi-Fi PHY (all hops): " << m_pcapPrefix << "-wifi-node<N>-if<J>-*.pcap\n"
            << "    (MonitorSniffer on each radio; includes HWMP forward frames)\n";
  if (m_pcapIp)
    {
      std::cout << "  IPv4 stack (endpoints only): " << m_pcapPrefix << "-ipv4-<node>-<if>.pcap\n"
                << "    (transit mesh nodes forward at L2; IP PCAP skips middle hops)\n";
    }
  std::cout << "  Gateway Emu: " << m_pcapPrefix << "-emu-*.pcap\n"
            << "Wireshark: wlan filters on -wifi-* files; icmp/udp on -ipv4-* at src/dst nodes\n";
}
void
MeshTest::InstallInternetStack ()
 {
   Ipv4AddressHelper address;
   address.SetBase ("10.1.1.0", "255.255.255.0");
   interfaces = address.Assign (meshDevices);

   Ipv4StaticRoutingHelper ipv4RoutingHelper;
   Ipv4Address meshGateway = interfaces.GetAddress (0);

   Ptr<Node> gwNode = nodes.Get (0);
   Ptr<Ipv4> ipv4Gw = gwNode->GetObject<Ipv4> ();
   ipv4Gw->SetAttribute ("IpForward", BooleanValue (true));

   Ptr<NetDevice> emuDev;
   for (uint32_t d = 0; d < gwNode->GetNDevices (); ++d)
     {
       emuDev = DynamicCast<FdNetDevice> (gwNode->GetDevice (d));
       if (emuDev)
         {
           break;
         }
     }
   NS_ASSERT (emuDev);

   Ptr<Ipv4StaticRouting> gwStatic = ipv4RoutingHelper.GetStaticRouting (ipv4Gw);
   int32_t emuIf = ipv4Gw->GetInterfaceForDevice (emuDev);
   NS_ASSERT (emuIf >= 0);
   gwStatic->SetDefaultRoute (Ipv4Address ("192.168.13.1"), emuIf);

   Ptr<MeshGatewayNatRouting> natRouting = CreateObject<MeshGatewayNatRouting> ();
   natRouting->SetStaticRouting (gwStatic);
   natRouting->SetOutsideDevice (emuDev);
   natRouting->SetOutsideAddress (Ipv4Address ("192.168.13.111"));
   natRouting->SetInsideNetwork (Ipv4Address ("10.1.1.0"), Ipv4Mask ("255.255.255.0"));
  ipv4Gw->SetRoutingProtocol (natRouting);

  Ptr<Ipv4L3Protocol> ipv4L3 = gwNode->GetObject<Ipv4L3Protocol> ();
  emuDev->SetPromiscReceiveCallback (
    MakeBoundCallback (&GatewayEmuPromiscRx, ipv4L3));

  for (uint32_t n = 1; n < nodes.GetN (); ++n)
     {
       Ptr<Ipv4> ipv4 = nodes.Get (n)->GetObject<Ipv4> ();
       int32_t meshIf = ipv4->GetInterfaceForDevice (meshDevices.Get (n));
       NS_ASSERT (meshIf >= 0);
       Ptr<Ipv4StaticRouting> rt = ipv4RoutingHelper.GetStaticRouting (ipv4);
       rt->SetDefaultRoute (meshGateway, meshIf);
     }
 }
 void
 MeshTest::InstallApplication ()
 {
   NS_ABORT_MSG_IF (m_rtpNode >= nodes.GetN (), "rtp-node out of range");

   NS_LOG_INFO ("Install RtpClient on node " << m_rtpNode << " -> "
                << m_rtpRemote << ":" << m_rtpPort);
   Address rtpRemote = InetSocketAddress (Ipv4Address (m_rtpRemote.c_str ()), m_rtpPort);
   RtpClientHelper rtpClient (rtpRemote);
   ApplicationContainer rtpApps = rtpClient.Install (nodes.Get (m_rtpNode));
   Ptr<RtpClient> rtp = rtpApps.Get (0)->GetObject<RtpClient> ();
   rtp->SetAttribute ("Interval", TimeValue (Seconds (m_rtpInterval)));
   rtp->SetAttribute ("PacketSize", UintegerValue (m_rtpPayloadSize));
   rtp->SetAttribute ("MaxPackets", UintegerValue (0));
   rtpApps.Start (Seconds (1.0));
   rtpApps.Stop (Seconds (m_totalTime + 1.0));
   std::cout << "RTP: node " << m_rtpNode << " -> " << m_rtpRemote << ":" << m_rtpPort
             << " every " << m_rtpInterval << "s for ~" << m_totalTime << "s"
             << " (use --pcap=0 with Realtime+Emu; pcap slows streaming)" << std::endl;

   if (m_enablePing) {
       NS_ABORT_MSG_IF (m_pingNode >= nodes.GetN (), "ping-node out of range");
       NS_LOG_INFO ("Install V4Ping on node " << m_pingNode << " -> " << m_pingRemote);
       Ptr<V4Ping> pingapp = CreateObject<V4Ping> ();
       pingapp->SetAttribute ("Remote", Ipv4AddressValue (m_pingRemote.c_str ()));
       pingapp->SetAttribute ("Verbose", BooleanValue (true));
       nodes.Get (m_pingNode)->AddApplication (pingapp);
       pingapp->SetStartTime (Seconds (2.0));
       pingapp->SetStopTime (Seconds (m_totalTime + 1.5));

       std::ostringstream pingName;
       pingName << "pingapp-" << m_pingNode;
       Names::Add (pingName.str (), pingapp);
       Config::Connect ("/Names/" + pingName.str () + "/Rtt", MakeCallback (&PingRtt));
   }
 }
 int
 MeshTest::Run ()
 {
  CreateNodes ();
  InstallInternetStack ();
  EnablePcapTraces ();
  InstallApplication ();
   Simulator::Schedule (Seconds (m_totalTime), &MeshTest::Report, this);
   Simulator::Stop (Seconds (m_totalTime + 2));
   Simulator::Run ();
   Simulator::Destroy ();
   std::cout << "UDP echo packets sent: " << g_udpTxCount << " received: " << g_udpRxCount << std::endl;
   return 0;
 }
 void
 MeshTest::Report ()
 {
   unsigned n (0);
   for (NetDeviceContainer::Iterator i = meshDevices.Begin (); i != meshDevices.End (); ++i, ++n)
     {
       std::ostringstream os;
       os << "mp-report-" << n << ".xml";
       std::cerr << "Printing mesh point device #" << n << " diagnostics to " << os.str () << "\n";
       std::ofstream of;
       of.open (os.str ().c_str ());
       if (!of.is_open ())
         {
           std::cerr << "Error: Can't open file " << os.str () << "\n";
           return;
         }
       mesh.Report (*i, of);
       of.close ();
     }
 }
 
 int
 main (int argc, char *argv[])
 {    
   MeshTest t; 
   GlobalValue::Bind(
    "SimulatorImplementationType",
    StringValue("ns3::RealtimeSimulatorImpl"));
   t.Configure (argc, argv);
   return t.Run ();
 }
 

