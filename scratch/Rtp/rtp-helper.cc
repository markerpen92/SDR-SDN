#include "rtp-helper.h"
#include "ns3/uinteger.h"
#include "ns3/names.h"

namespace ns3 {

RtpServerHelper::RtpServerHelper (Address address)
    : m_address (address) {
}

ApplicationContainer RtpServerHelper::Install (NodeContainer c) {
    ApplicationContainer apps;
    for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i) {
        Ptr<Node> node = *i;
        Ptr<Application> app = InstallPriv (node);
        apps.Add (app);
    }
    return apps;
}

Ptr<Application> RtpServerHelper::InstallPriv (Ptr<Node> node) {
    Ptr<RtpServer> server = CreateObject<RtpServer> ();
    server->SetAttribute ("Local", AddressValue (m_address));
    node->AddApplication (server);
    return server;
}

RtpClientHelper::RtpClientHelper (Address address)
    : m_address (address) {
}

ApplicationContainer RtpClientHelper::Install (NodeContainer c) {
    ApplicationContainer apps;
    for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i) {
        Ptr<Node> node = *i;
        Ptr<Application> app = InstallPriv (node);
        apps.Add (app);
    }
    return apps;
}

Ptr<Application> RtpClientHelper::InstallPriv (Ptr<Node> node) {
    Ptr<RtpClient> client = CreateObject<RtpClient> ();
    client->SetAttribute ("Remote", AddressValue (m_address));
    node->AddApplication (client);
    return client;
}

} // namespace ns3
