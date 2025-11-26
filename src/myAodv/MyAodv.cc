#include "MyAodv.h"
#include "inet/routing/aodv/AodvControlPackets_m.h"
#include "inet/networklayer/ipv4/Ipv4InterfaceData.h"

void MyAodv::initialize(int stage)
{
    Aodv::initialize(stage);

    if (stage == INITSTAGE_ROUTING_PROTOCOLS) {
        cModule *host = getContainingNode(this);

        cModule *agentModule = host->getSubmodule("DominatingSetAgent");

        if (agentModule) {
            dominatingSetAgent = check_and_cast<DominatingSetAgent*>(agentModule);
        } else {
            EV_ERROR << "DominatingSetAgent not found in this node! AODV will run in standard mode.\n";
        }
    }
}

void MyAodv::handleRREQ(const Ptr<Rreq>& rreq, const L3Address& sourceAddr, unsigned int timeToLive)
{
    EV_WARN << ">>> MY CUSTOM AODV IS RUNNING! <<<\n"; // not running but trying to fix in full implementation

    Ipv4Address destAddr = rreq->getDestAddr().toIpv4();

    Ipv4Address myAddr;

    if (this->interfaceTable) {
        auto *interfaceData = this->interfaceTable->findFirstNonLoopbackInterface()->getProtocolData<Ipv4InterfaceData>();
        myAddr = interfaceData->getIPAddress();
    }
    else {
        throw cRuntimeError("MyAodv: Interface Table not found!");
    }

    if (dominatingSetAgent != nullptr) {
        bool isHighway = dominatingSetAgent->isDominatingNode();
        bool isDestination = (destAddr == myAddr);

        if (!isHighway && !isDestination) {
            EV_INFO << "CDS-AODV: Node " << myAddr << " is not a backbone node. Ignoring RREQ.\n";
            return;
        }
    }
    Aodv::handleRREQ(rreq, sourceAddr, timeToLive);
}

Define_Module(MyAodv);
