#include "openwifi-netdevice-interface.h"
#include "openwifi-interface.h"

OpenwifiNetDeviceInterface::OpenwifiNetDeviceInterface(
        Ptr<FdNetDevice> dev, OpenwifiInterface* inter){
    m_fd_netdevice = dev;
    m_openwifi_interface = inter;
}

Ptr<FdNetDevice> OpenwifiNetDeviceInterface::GetFdNetDevice(){
    return m_fd_netdevice;
}

OpenwifiInterface* OpenwifiNetDeviceInterface::GetOpenwifiInterface(){
    return m_openwifi_interface;
}