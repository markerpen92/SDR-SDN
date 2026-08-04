#pragma once

#include "ns3/core-module.h"
#include "ns3/fd-net-device-module.h"

#include "openwifi-interface.h"
#include "openwifi-netdevice-interface.h"

using namespace ns3;

class OpenwifiNetDeviceInterfaceHepler {
    public:
        OpenwifiNetDeviceInterfaceHepler();
        std::string GetDeviceName();
        void SetDeviceName(std::string deviceName);

        OpenwifiNetDeviceInterface* Install(Ptr<Node> node);

        void EnablePcap(std::string prefix, Ptr<NetDevice> nd, bool promiscuous, bool explicitFilename);
    
    private:
        EmuFdNetDeviceHelper* m_emu_helper;
};

#include "openwifi-netdevice-interface-helper.cc"