/*
 * Copyright (c) 2017 Universita' degli Studi di Napoli Federico II
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
 * Author: Pasquale Imputato <p.imputato@gmail.com>
 */

#include "openwifi-netdevice-interface-helper.h"

#include "ns3/abort.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/fd-net-device-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/network-module.h"
#include "ns3/trace-helper.h"
#include "ns3/traffic-control-module.h"

#include <chrono>
#include <unistd.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EmulationFwd");

std::string deviceName("sdr0");
Ipv4Address network_eth("192.168.10.0");
Ipv4Address host_addr_eth("192.168.10.227");
Ipv4Address network_wifi("192.168.13.0");
Ipv4Address host_addr_wifi("192.168.13.2");
Ipv4Address target_addr_1("192.168.13.5"); // laplop
Ipv4Address target_addr_2("192.168.13.7"); // respberry

static void
SetTxAttenuationDb(OpenwifiInterface* opnewifi_inter, float atten)
{
    auto time_cur = std::chrono::system_clock::now();
    NS_LOG_UNCOND("Set TX attenuation to " << atten
                   << " at time " << Simulator::Now().As(Time::Unit::S)
                   << " realtime time " 
                   << std::chrono::duration_cast<std::chrono::seconds>(
                        time_cur.time_since_epoch()).count());
    opnewifi_inter->set_register(REG_TX_ATTENUATION, (unsigned int)(-atten * 1000));
    // unsigned int val = opnewifi_inter->get_register(REG_TX_ATTENUATION);
}

// void
// MacTxCallback(OpenwifiInterface* opnewifi_inter, Ptr<const Packet> pkt){
//     Ptr<Packet> copy = pkt->Copy();
//     EthernetHeader ethHeader;
//     copy->RemoveHeader(ethHeader);
//     Ipv4Header ipv4Header;
//     copy->RemoveHeader(ipv4Header);
//     if(ipv4Header.GetDestination() == target_addr_1){
//         // laptop
//         SetTxAttenuationDb(opnewifi_inter, -20);
//     }else{
//         // else
//         SetTxAttenuationDb(opnewifi_inter, 0);
//     }
//     return;
// }

int
main(int argc, char* argv[])
{
    std::string emuMode("raw");

    CommandLine cmd;
    // cmd.AddValue("runTime", "Run time", runTime);

    GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::RealtimeSimulatorImpl"));
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1<<19)); // defult was 131072=2**17

    cmd.Parse(argc, argv);
    NS_LOG_UNCOND("Create Node");
    Ptr<Node> node = CreateObject<Node>();
    InternetStackHelper stack;
    stack.Install(node);


    NS_LOG_UNCOND("Create Device");
    OpenwifiNetDeviceInterfaceHepler* helper = new OpenwifiNetDeviceInterfaceHepler();
    helper->SetDeviceName(deviceName);

    OpenwifiNetDeviceInterface* openwifi_device_interface = helper->Install(node);
    Ptr<NetDevice> device_wifi = openwifi_device_interface->GetFdNetDevice();
    OpenwifiInterface* openwifiInterface = openwifi_device_interface->GetOpenwifiInterface();
    helper->EnablePcap("capture_wifi.pcap",
                       device_wifi,
                       /* promisc */ true,
                       /* use explicit filename */ true);
    
    EmuFdNetDeviceHelper* helper_eth = new EmuFdNetDeviceHelper();
    helper_eth->SetDeviceName("eth0");

    Ptr<FdNetDevice> device_eth = DynamicCast<FdNetDevice>(helper_eth->Install(node).Get(0));
    helper_eth->EnablePcap("capture_eth.pcap",
                       device_eth,
                       /* promisc */ true,
                       /* use explicit filename */ true);


    NS_LOG_UNCOND("Get IPV4");
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();

    NS_LOG_UNCOND("Assign address");
    int32_t ifIndex_wifi = 0;
    ifIndex_wifi = ipv4->GetInterfaceForDevice(device_wifi);
    if (ifIndex_wifi == -1)
    {
        ifIndex_wifi = ipv4->AddInterface(device_wifi);
    }
    Ipv4InterfaceAddress ipv4AddrWifi = Ipv4InterfaceAddress(host_addr_wifi, Ipv4Mask("/24"));
    ipv4->AddAddress(ifIndex_wifi, ipv4AddrWifi);
    ipv4->SetMetric(ifIndex_wifi, 1);
    ipv4->SetUp(ifIndex_wifi);
    ipv4->SetForwarding(ifIndex_wifi, true);

    int32_t ifIndex_eth = 0;
    ifIndex_eth = ipv4->GetInterfaceForDevice(device_eth);
    if (ifIndex_eth == -1)
    {
        ifIndex_eth = ipv4->AddInterface(device_eth);
    }
    Ipv4InterfaceAddress ipv4Addr = Ipv4InterfaceAddress(host_addr_eth, Ipv4Mask("/24"));
    ipv4->AddAddress(ifIndex_eth, ipv4Addr);
    ipv4->SetMetric(ifIndex_eth, 1);
    ipv4->SetUp(ifIndex_eth);
    ipv4->SetForwarding(ifIndex_eth, true);

    // Ipv4StaticRoutingHelper ipv4RoutingHelper;
    // Ptr<Ipv4StaticRouting> staticRouting = ipv4RoutingHelper.GetStaticRouting(ipv4);
    // staticRouting->AddNetworkRouteTo(network_wifi, Ipv4Mask("/24"), ifIndex_wifi);
    // staticRouting->AddNetworkRouteTo(network_eth, Ipv4Mask("/24"), ifIndex_eth);
    // staticRouting->PrintRoutingTable(new OutputStreamWrapper(&std::cout));

    // device_wifi->TraceConnectWithoutContext(
    //     "MacTx", MakeBoundCallback(&MacTxCallback, openwifiInterface));

    // Schedule TX changes
    Simulator::Schedule(Seconds(0), &SetTxAttenuationDb, openwifiInterface, 0);
    float time = 10; 
    for(int i = 0; i<8; i++){
        Simulator::Schedule(Seconds(time), &SetTxAttenuationDb, openwifiInterface, -5*i);
        time += 10; 
    }
    // time = 90;
    Simulator::Schedule(Seconds(time), &SetTxAttenuationDb, openwifiInterface, 0);
    NS_LOG_UNCOND("Stop at " << time);

    NS_LOG_UNCOND("Run Emulation.");
    Simulator::Stop(Seconds(time));
    Simulator::Run();
    Simulator::Destroy();
    delete openwifiInterface;
    NS_LOG_UNCOND("Done.");
}
