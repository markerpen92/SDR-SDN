#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "openwifi-netdevice-interface-helper.h"


std::string getMacAddress(const std::string interfaceName) {
    std::string macAddress;
    std::ifstream file("/sys/class/net/" + interfaceName + "/address");
    
    if (file.is_open()) {
        std::getline(file, macAddress);
        file.close();
    } else {
        std::cerr << "Failed to open MAC address file for interface " << interfaceName << std::endl;
    }
    
    return macAddress;
}


OpenwifiNetDeviceInterfaceHepler::OpenwifiNetDeviceInterfaceHepler(){
    m_emu_helper = new EmuFdNetDeviceHelper();
}

std::string OpenwifiNetDeviceInterfaceHepler::GetDeviceName(){
    return m_emu_helper->GetDeviceName();
}

void OpenwifiNetDeviceInterfaceHepler::SetDeviceName(std::string deviceName){
    m_emu_helper->SetDeviceName(deviceName);
}

OpenwifiNetDeviceInterface* OpenwifiNetDeviceInterfaceHepler::Install(Ptr<Node> node){
    NetDeviceContainer devices = m_emu_helper->Install(node);
    Ptr<NetDevice> device = devices.Get(0);

    // std::string mac_address(getMacAddress(GetDeviceName()));
    // device->SetAttribute("Address", Mac48AddressValue(mac_address.c_str()));

    OpenwifiInterface* openwifiInterface = new OpenwifiInterface(GetDeviceName());

    OpenwifiNetDeviceInterface* device_inter = 
        new OpenwifiNetDeviceInterface(DynamicCast<FdNetDevice>(device), openwifiInterface);
    return device_inter;
}

void OpenwifiNetDeviceInterfaceHepler::EnablePcap(
        std::string prefix, Ptr<NetDevice> nd, bool promiscuous, bool explicitFilename){
    m_emu_helper->EnablePcap(prefix, nd, promiscuous, explicitFilename);
}
