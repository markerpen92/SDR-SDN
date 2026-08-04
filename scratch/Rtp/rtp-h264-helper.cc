#include "rtp-h264-helper.h"

namespace ns3 {

RtpH264ClientHelper::RtpH264ClientHelper (Address address)
  : m_address (address)
{
}

ApplicationContainer
RtpH264ClientHelper::Install (NodeContainer c)
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      apps.Add (InstallPriv (*i));
    }
  return apps;
}

Ptr<Application>
RtpH264ClientHelper::InstallPriv (Ptr<Node> node)
{
  Ptr<RtpH264Client> client = CreateObject<RtpH264Client> ();
  client->SetAttribute ("Remote", AddressValue (m_address));
  node->AddApplication (client);
  return client;
}

} // namespace ns3
