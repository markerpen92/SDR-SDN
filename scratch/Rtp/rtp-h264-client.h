#ifndef RTP_H264_CLIENT_H
#define RTP_H264_CLIENT_H

#include "ns3/application.h"
#include "ns3/address.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/nstime.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {

/**
 * Streams a pre-encoded Annex-B H.264 elementary stream as RTP/H264 (RFC 6184)
 * for receivers such as GStreamer udpsrc ! rtph264depay (payload type 96).
 */
class RtpH264Client : public Application
{
public:
  static TypeId GetTypeId (void);
  RtpH264Client ();
  ~RtpH264Client () override;

protected:
  void StartApplication () override;
  void StopApplication () override;

private:
  struct NalUnit
  {
    std::vector<uint8_t> data;
    uint8_t type;
    bool vcl;
  };

  bool LoadH264File (const std::string &path);
  void SendConfig (void);
  void SendNextAccessUnit (void);
  void SendNal (const NalUnit &nal, bool marker);
  void SendSingleNal (const uint8_t *nal, uint32_t nalSize, bool marker);
  void SendFuA (const uint8_t *nal, uint32_t nalSize, bool marker);
  Ptr<Packet> BuildRtpPacket (const uint8_t *payload, uint32_t payloadSize, bool marker);
  void ScheduleNextAccessUnit (void);
  void ReportStats (void);

  Ptr<Socket> m_socket;
  Address m_peer;
  EventId m_sendEvent;
  EventId m_statsEvent;
  std::string m_videoFile;
  std::vector<NalUnit> m_nals;
  std::vector<uint8_t> m_sps;
  std::vector<uint8_t> m_pps;
  uint32_t m_accessUnitIndex;
  double m_fps;
  double m_configInterval;
  uint32_t m_maxRtpPayload;
  uint32_t m_maxPackets;
  uint32_t m_sent;
  uint16_t m_seq;
  uint32_t m_timestamp;
  uint32_t m_ssrc;
  uint8_t m_payloadType;
  Time m_lastConfigSent;
};

} // namespace ns3

#endif // RTP_H264_CLIENT_H
