#ifndef RTP_H264_HELPER_H
#define RTP_H264_HELPER_H

#include "ns3/application-container.h"
#include "ns3/node-container.h"
#include "rtp-h264-client.h"

namespace ns3 {

class RtpH264ClientHelper
{
public:
  RtpH264ClientHelper (Address address);
  ApplicationContainer Install (NodeContainer c);

private:
  Ptr<Application> InstallPriv (Ptr<Node> node);
  Address m_address;
};

} // namespace ns3

#endif // RTP_H264_HELPER_H
