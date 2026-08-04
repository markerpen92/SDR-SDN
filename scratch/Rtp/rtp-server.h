#ifndef RTP_SERVER_H

#define RTP_SERVER_H

#include "ns3/application.h"
#include "ns3/address.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"

namespace ns3 {

class RtpServer : public Application {

public:
    static TypeId GetTypeId (void);
    RtpServer ();
    virtual ~RtpServer ();

protected:
    virtual void StartApplication (void);
    virtual void StopApplication (void);

private:
    void HandleRead (Ptr<Socket> socket);
    void ReportStats ();
    bool ParseRtpHeader (Ptr<Packet> packet, uint16_t &seq, uint32_t &ts,
                         uint32_t &ssrc, uint8_t &pt) const;

    Ptr<Socket> m_socket;
    Address m_local;
    Address m_forwardRemote;
    bool m_enableForward;
    EventId m_statsEvent;
    uint32_t m_received;
    uint32_t m_forwarded;
    uint16_t m_lastSeq;
    bool m_haveLastSeq;
};

} // namespace ns3

#endif // RTP_SERVER_H
