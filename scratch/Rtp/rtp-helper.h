#ifndef RTP_HELPER_H

#define RTP_HELPER_H

#include "ns3/application-container.h"
#include "ns3/node-container.h"
#include "rtp-server.h"
#include "rtp-client.h"

namespace ns3 {

class RtpServerHelper {

public:
    RtpServerHelper (Address address);
    ApplicationContainer Install (NodeContainer c);

private:
    Ptr<Application> InstallPriv (Ptr<Node> node);
    Address m_address;
};

class RtpClientHelper {

public:
    RtpClientHelper (Address address);
    ApplicationContainer Install (NodeContainer c);

private:
    Ptr<Application> InstallPriv (Ptr<Node> node);
    Address m_address;
};

} // namespace ns3

#endif // RTP_HELPER_H
