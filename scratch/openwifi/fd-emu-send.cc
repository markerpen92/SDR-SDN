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

#include "ns3/abort.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/fd-net-device-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/network-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/trace-helper.h"

#include "openwifi-netdevice-interface-helper.h"

#include <chrono>
#include <unistd.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EmulationSend");

uint32_t seq = 0;

static void
Send(Ptr<NetDevice> dev, int level)
{
    double interval_ms = 1;
    Simulator::Schedule(MilliSeconds(interval_ms), &Send, dev, level);

    Ptr<FdNetDevice> device = DynamicCast<FdNetDevice>(dev);

    Mac48AddressValue address_attribute;
    dev->GetAttribute("Address", address_attribute);

    Mac48Address sender = address_attribute.Get();
    Mac48Address receiver = Mac48Address("ff:ff:ff:ff:ff:ff");

    int packetsSize = 64;
    int packetsSize32 = packetsSize/sizeof(uint32_t);
    uint32_t* buffer_prep = (uint32_t*) calloc(packetsSize32, sizeof(uint32_t));
    buffer_prep[packetsSize32-1] = seq;

    Ptr<Packet> packet = Create<Packet>((uint8_t*) buffer_prep, packetsSize);
    EthernetHeader header;

    ssize_t len = (size_t)packet->GetSize();
    uint8_t* buffer = (uint8_t*)malloc(len);
    packet->CopyData(buffer, len);

    static int sent = 0;
    static int failed = 0;

    // std::cout << ((level == 0) ? "Writing" : "Sending") << std::endl;


    if (level == 1)
    {
        if (device->SendFrom(packet, sender, receiver, 0) == false)
        {
            failed++;
        }
        sent++;
        packet->RemoveHeader(header);
    }

    if (level == 0)
    {
        if (device->Write(buffer, len) != len)
        {
            failed++;
        }
        sent++;
    }

    // period to print the stats
    std::chrono::milliseconds period_stat(1000);

    static auto last_stat = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    if (now - last_stat >= period_stat)
    {
        // print stats
        std::chrono::duration<double, std::milli> dur = (now - last_stat);           // in ms
        // double estimatedThr = ((sent - failed) * packetsSize * 8) / 1000000; // in Mbps
        std::cout << sent << " packets sent in " << dur.count() << " ms, failed " << failed << std::endl;
                    // << " (" << estimatedThr << " Mbps estimated throughput)" << std::endl;
        sent = 0;
        failed = 0;
        last_stat = now;
    }

    seq ++;
}

static void
SetTxAttenuationDb(OpenwifiInterface* opnewifi_inter, float atten){
    std::cout << "Set Tx Atten to " << atten << " before seq " << seq << std::endl;
    opnewifi_inter->set_register(
        REG_TX_ATTENUATION, (unsigned int)(-atten*1000) );
    // unsigned int val = opnewifi_inter->get_register(REG_TX_ATTENUATION);
}

int
main(int argc, char* argv[])
{
    std::string deviceName("sdr0");

    std::string emuMode("raw");
    int case_select = 1;

    CommandLine cmd;
    cmd.AddValue("deviceName", "Device name", deviceName);
    cmd.AddValue("case", "Case to run", case_select);
    NS_ASSERT(case_select==1 || case_select==2);

    GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::RealtimeSimulatorImpl"));
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

    cmd.Parse(argc, argv);
    NS_LOG_INFO("Create Node");
    Ptr<Node> node = CreateObject<Node>();

    NS_LOG_INFO("Create Device");

    OpenwifiNetDeviceInterfaceHepler* helper = new OpenwifiNetDeviceInterfaceHepler();
    helper->SetDeviceName(deviceName);

    OpenwifiNetDeviceInterface* openwifi_device_interface = helper->Install(node);
    Ptr<NetDevice> device = openwifi_device_interface->GetFdNetDevice();
    OpenwifiInterface* openwifiInterface = openwifi_device_interface->GetOpenwifiInterface();

    double time = 4;
    double atten = 0;
    switch (case_select)
    {
    case 1:
        for (time=5; time<30; time+=5){
            std::cout << "Schedule Tx Atten to " << atten << " at " << time << std::endl;
            Simulator::Schedule(Seconds(time), &SetTxAttenuationDb, openwifiInterface, atten);
            atten -= 6;
        }
        std::cout << "Schedule Tx Atten to " << 0 << " at " << time << std::endl;
        Simulator::Schedule(Seconds(time), &SetTxAttenuationDb, openwifiInterface, 0);
        break;

    case 2:
        for (double d=2; d>1e-3; d/=2){
            std::cout << "Schedule Tx Atten to " << 0 << " at " << time << std::endl;
            Simulator::Schedule(Seconds(time), &SetTxAttenuationDb, openwifiInterface, 0);
            time += d;
            std::cout << "Schedule Tx Atten to " << -6 << " at " << time << std::endl;
            Simulator::Schedule(Seconds(time), &SetTxAttenuationDb, openwifiInterface, -6);
            time += d;
        }
        std::cout << "Schedule Tx Atten to " << 0 << " at " << time << std::endl;
        Simulator::Schedule(Seconds(time), &SetTxAttenuationDb, openwifiInterface, 0);
        break;
    
    default:
        NS_FATAL_ERROR("Uncaught invalid case. This should not happen.");
        break;
    }


    //auto _ = &Send; _ = _;
    Simulator::Schedule(Seconds(2), &Send, device, 1);

    helper->EnablePcap("capture.pcap", device, 
        /* promisc */ true, /* use explicit filename */ true);

    NS_LOG_INFO("Run Emulation.");
    Simulator::Stop(Seconds(time+4));
    Simulator::Run();
    Simulator::Destroy();
   delete openwifiInterface;
    NS_LOG_INFO("Done.");
}
