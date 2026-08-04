#include "openwifi-interface.h"
#include "ns3/core-module.h"

#include <net/if.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TestInclude");

int main(int argc, char* argv[]) {
    unsigned int devidx = if_nametoindex("sdr0");
    if (devidx == 0){
        std::cerr << "device not found\n";
        return 1;
    }

	OpenwifiInterface openwifi_inter(devidx);

    unsigned int reg_addr, reg_val;
    // rf 0: tx attenuation in 1/1000 dB
    reg_addr = REG_TX_ATTENUATION;
    reg_val = 1000;  // 1000/1000 dB

    unsigned int read_val;

    std::cout << "Getting:\n";
    read_val = openwifi_inter.get_register(reg_addr);
    std::cout << "reg val: " << std::hex << read_val << std::dec << "\n\n";

    std::cout << "Setting\n";
    openwifi_inter.set_register(reg_addr, reg_val);

    std::cout << "Getting:\n";
    read_val = openwifi_inter.get_register(reg_addr);
    std::cout << "reg val: " << std::hex << read_val << std::dec << "\n\n";
    
    return 0;
}
