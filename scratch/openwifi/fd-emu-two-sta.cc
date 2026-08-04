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

NS_LOG_COMPONENT_DEFINE("EmulationTwoSta");

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


int
main(int argc, char* argv[])
{
    std::string deviceName("sdr0");
    Ipv4Address host_addr("192.168.13.2");
    Ipv4Address target_addr_1("192.168.13.4");
    Ipv4Address target_addr_2("192.168.13.5");

    std::string emuMode("raw");

    CommandLine cmd;
    cmd.AddValue("deviceName", "Device name", deviceName);
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
    Ptr<NetDevice> device = openwifi_device_interface->GetFdNetDevice();
    OpenwifiInterface* openwifiInterface = openwifi_device_interface->GetOpenwifiInterface();

    helper->EnablePcap("capture.pcap",
                       device,
                       /* promisc */ true,
                       /* use explicit filename */ true);

    NS_LOG_UNCOND("Get IPV4");
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    int32_t ifIndex = 0;
    NS_LOG_UNCOND("Get index");
    ifIndex = ipv4->GetInterfaceForDevice(device);
    if (ifIndex == -1)
    {
        ifIndex = ipv4->AddInterface(device);
    }
    NS_LOG_UNCOND(ifIndex);
    Ipv4InterfaceAddress ipv4Addr = Ipv4InterfaceAddress(host_addr, Ipv4Mask("/24"));
    NS_LOG_UNCOND("Assign address");
    ipv4->AddAddress(ifIndex, ipv4Addr);
    ipv4->SetMetric(ifIndex, 1);
    ipv4->SetUp(ifIndex);

    NS_LOG_UNCOND("Set up app");
    uint16_t port = 2277;
    BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(target_addr_1, port));
    source.SetAttribute("EnableSeqTsSizeHeader", BooleanValue(true));
    source.SetAttribute("MaxBytes", UintegerValue(0));
    source.SetAttribute("SendSize", UintegerValue(1400));
    Ptr<Application> source_apps_1 = source.Install(node).Get(0);
    source.SetAttribute("Remote", AddressValue(InetSocketAddress(target_addr_2, port)));
    Ptr<Application> source_apps_2 = source.Install(node).Get(0);

    ApplicationContainer apps = ApplicationContainer();
    apps.Add(source_apps_1);
    apps.Add(source_apps_2);

    apps.Start(Seconds(5));
    float time = 15; 
    for(int i = 0; i<8; i++){
        Simulator::Schedule(Seconds(time), &SetTxAttenuationDb, openwifiInterface, -3*i);
        time += 10; 
    }
    Simulator::Schedule(Seconds(time), &SetTxAttenuationDb, openwifiInterface, 0);

    apps.Stop(Seconds(time+5));
    NS_LOG_UNCOND("Stop at " << time);

    NS_LOG_UNCOND("Run Emulation.");
    Simulator::Stop(Seconds(time));
    Simulator::Run();
    Simulator::Destroy();
    delete openwifiInterface;
    NS_LOG_UNCOND("Done.");
}
