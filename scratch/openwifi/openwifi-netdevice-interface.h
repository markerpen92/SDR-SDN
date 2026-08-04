#pragma once

#include "ns3/core-module.h"
#include "ns3/fd-net-device-module.h"
#include "openwifi-interface.h"

using namespace ns3;

class OpenwifiNetDeviceInterface {
    public:
        OpenwifiNetDeviceInterface(Ptr<FdNetDevice> dev, OpenwifiInterface* inter);

        Ptr<FdNetDevice> GetFdNetDevice();
        OpenwifiInterface* GetOpenwifiInterface();

    private:
        Ptr<FdNetDevice> m_fd_netdevice;
        OpenwifiInterface* m_openwifi_interface;
};

#include "openwifi-netdevice-interface.cc"