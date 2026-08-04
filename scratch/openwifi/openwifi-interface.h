#pragma once

struct nl80211_state {
    struct nl_sock *nl_sock;
    int nl80211_id;
};

enum RegSystem: unsigned int {
    RF = 1,
    RX_INTF, TX_INTF,
    RX, TX, XPU,
    DRV_RX, DRV_TX, DRV_XPU
};

#define REG(subsystem, index) ((subsystem) << 16) | ((index) << 2)

// rf 0: tx attenuation in 1/1000 dB
const int REG_TX_ATTENUATION = REG(RegSystem::RF, 0);

class OpenwifiInterface {
    public:
        OpenwifiInterface(unsigned int devidx);
        OpenwifiInterface(std::string deviceName);
        ~OpenwifiInterface();

        void set_register(unsigned int reg_addr, unsigned int reg_val);
        unsigned int get_register(unsigned int reg_addr);

    private:
        void _set_register(unsigned int reg_addr, unsigned int reg_val);
        unsigned int _get_register(unsigned int reg_addr);

        struct nl80211_state m_nlstate;
        unsigned int m_devidx;
};

#include "openwifi-interface.cc"