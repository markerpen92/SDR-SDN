#ifndef RTP_CLIENT_H

#define RTP_CLIENT_H

#include "ns3/application.h"
#include "ns3/address.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"
#include "ns3/nstime.h"

namespace ns3 {

class RtpClient : public Application {

public:
    static TypeId GetTypeId (void);
    RtpClient ();
    virtual ~RtpClient ();

protected:
    virtual void StartApplication (void);
    virtual void StopApplication (void);

private:
    Ptr<Packet> BuildRtpPacket ();
    void SendRequest ();
    void ScheduleNext ();
    void ReportStats ();
    Ptr<Socket> m_socket;
    Address m_peer;
    EventId m_sendEvent;
    EventId m_statsEvent;
    Time m_interval;
    uint32_t m_payloadSize;
    uint32_t m_maxPackets;
    uint32_t m_sent;
    uint16_t m_seq;
    uint32_t m_timestamp;
    uint32_t m_ssrc;
    uint8_t m_payloadType;
};

} // namespace ns3

#endif // RTP_CLIENT_H
