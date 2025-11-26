#include "DominatingSetAgent.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/packet/Packet.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/networklayer/contract/IInterfaceTable.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/networklayer/ipv4/Ipv4InterfaceData.h"
#include "inet/common/Units.h"
#include "inet/transportlayer/common/L4PortTag_m.h"
#include "inet/networklayer/common/HopLimitTag_m.h"
#include "inet/mobility/contract/IMobility.h"
#include <climits>
#include <limits>

Define_Module(DominatingSetAgent);

DominatingSetAgent::~DominatingSetAgent()
{
    cancelAndDelete(helloTimer);
    cancelAndDelete(algorithmTimer);
}

void DominatingSetAgent::initialize(int stage)
{
    ApplicationBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        helloInterval = par("helloInterval").doubleValue();
        algorithmInterval = par("algorithmInterval").doubleValue();
        neighborPurgeTimeout = par("neighborPurgeTimeout").doubleValue();
        localPort = par("localPort");
        destPort = par("destPort");

        EV_INFO << "DominatingSetAgent initializing (LOCAL stage)..." << endl;
    }
    else if (stage == INITSTAGE_APPLICATION_LAYER) {
        cModule *host = getContainingNode(this);
        auto interfaceTable = check_and_cast<IInterfaceTable *>(host->getSubmodule("interfaceTable"));

        NetworkInterface *interface = nullptr;
        for (int i = 0; i < interfaceTable->getNumInterfaces(); i++) {
            auto ie = interfaceTable->getInterface(i);
            if (ie->isWireless() && !ie->isLoopback()) {
                interface = ie;
                break;
            }
        }

        if (!interface) {
            throw cRuntimeError("No wireless interface found");
        }

        myAddress = interface->getNetworkAddress();
        myId = myAddress.toIpv4().getInt();

        wirelessInterface = interface;
        wirelessInterfaceId = interface->getInterfaceId();
        auto ipv4Data = interface->findProtocolData<Ipv4InterfaceData>();
        if (ipv4Data != nullptr) {
            Ipv4Address bcast = ipv4Data->getNetworkBroadcastAddress();
            if (bcast == ipv4Data->getIPAddress())
                broadcastAddress = Ipv4Address::ALLONES_ADDRESS;
            else
                broadcastAddress = L3Address(bcast);
        }
        else {
            broadcastAddress = Ipv4Address::ALLONES_ADDRESS;
        }

        EV_INFO << "DS Agent initialized for node " << myAddress << " (ID: " << myId << ")" << endl;
        EV_INFO << "Broadcast address: " << broadcastAddress << ", Interface ID: " << wirelessInterfaceId << endl;

        helloTimer = new cMessage("sendHelloTimer");
        algorithmTimer = new cMessage("runAlgorithmTimer");

        socket.setCallback(this);
        socket.setOutputGate(gate("socketOut"));
        bindSocket();

        simtime_t firstHelloTime = simTime() + uniform(0, helloInterval);
        simtime_t firstAlgoTime = simTime() + uniform(0, algorithmInterval);
        scheduleAt(firstHelloTime, helloTimer);
        scheduleAt(firstAlgoTime, algorithmTimer);
        EV_INFO << "Node " << myAddress << " scheduled first HELLO at " << firstHelloTime
                << ", first algorithm run at " << firstAlgoTime << endl;


        previousClusterHead = L3Address();
        clusterHeadStartTime = -1.0;
    }
}

void DominatingSetAgent::handleMessageWhenUp(cMessage *msg)
{
    if (msg->isSelfMessage()) {
        if (msg == helloTimer) {
            broadcastHello();
            purgeNeighborTable();
            scheduleAt(simTime() + helloInterval, helloTimer);
        }
        else if (msg == algorithmTimer) {
            runClusteringAlgorithm();
            updateClusterMembership();
            detectGateway();
            scheduleAt(simTime() + algorithmInterval, algorithmTimer);
        }
    }
    else {
        EV_INFO << "Socket handled message(handleMessageWhenUp): " << msg->getName() << endl;
        socket.processMessage(msg);
    }
}

void DominatingSetAgent::socketDataArrived(UdpSocket *socket, Packet *packet)
{
    EV_INFO << "Node " << myAddress << " socketDataArrived called! Packet: " << packet->getName() << endl;

    auto l4PortTag = packet->findTag<L4PortInd>();
    int destPort = l4PortTag ? l4PortTag->getDestPort() : -1;

    const auto& payload = packet->peekAtFront<DsHelloPacket>();

    L3Address sender;
    if (!L3AddressResolver().tryResolve(payload->getSenderAddress(), sender)) {
        EV_WARN << "Could not parse sender address: " << payload->getSenderAddress() << endl;
        delete packet;
        return;
    }

    if (sender == myAddress) {
        delete packet;
        return;
    }

    EV_INFO << "Node " << myAddress << " received HELLO from " << sender << " (state=" << payload->getState() << ")" << endl;

    NeighborInfo& info = neighborTable[sender];
    info.lastHeard = simTime();
    info.state = (NodeState)payload->getState();

    info.position = Coord(payload->getPositionX(), payload->getPositionY(), payload->getPositionZ());

    info.oneHopNeighbors.clear();
    for (unsigned int i = 0; i < payload->getNeighborsArraySize(); i++) {
        L3Address neighborAddr;
        if (L3AddressResolver().tryResolve(payload->getNeighbors(i), neighborAddr)) {
            info.oneHopNeighbors.insert(neighborAddr);
        }
    }

    packet->popAtFront(payload->getChunkLength());
    delete packet;

    emit(helloReceivedSignal, 1);
    helloReceivedCount++;
}

void DominatingSetAgent::finish()
{
    recordScalar("helloReceivedCount", helloReceivedCount);
    ApplicationBase::finish();
}

void DominatingSetAgent::broadcastHello()
{
    if (!socketBound) {
        EV_WARN << "Socket not bound when attempting to broadcast. Binding now..." << endl;
        bindSocket();
    }

    EV_INFO << "Node " << myAddress << " broadcasting HELLO (State: " << myState << ")" << endl;

    auto payload = makeShared<DsHelloPacket>();
    payload->setSenderAddress(myAddress.str().c_str());
    payload->setState(myState);

    std::set<L3Address> neighbors = getMyOneHopNeighbors();
    payload->setNeighborsArraySize(neighbors.size());
    int i = 0;
    for (const auto& addr : neighbors) {
        payload->setNeighbors(i++, addr.str().c_str());
    }

    // If we do the routing - just in case
    //Coord myPos = getMyPosition();
    payload->setPositionX(0);//myPos.x);
    payload->setPositionY(0);//myPos.y);
    payload->setPositionZ(0);//myPos.z);

    payload->setChunkLength(B(32 + 4 * neighborTable.size()));

    Packet *packet = new Packet("DsHello", payload);

    if (wirelessInterfaceId >= 0) {
        packet->addTag<InterfaceReq>()->setInterfaceId(wirelessInterfaceId);
    }
    // Broadcast
    L3Address destAddr = broadcastAddress.isUnspecified() ? Ipv4Address::ALLONES_ADDRESS : broadcastAddress;
    packet->addTag<L3AddressReq>()->setDestAddress(destAddr);
    packet->addTag<L4PortReq>()->setDestPort(destPort);

    EV_INFO << "Node " << myAddress << " sending HELLO to " << destAddr << ":" << destPort
            << " via interface " << wirelessInterfaceId << endl;
    try {
        socket.send(packet);
        EV_INFO << "Node " << myAddress << " successfully sent HELLO packet" << endl;
    }
    catch (const cRuntimeError& e) {
        delete packet;
        EV_ERROR << "Failed to send HELLO from node " << myAddress << " on port " << localPort
                 << ": " << e.what() << endl;
        throw cRuntimeError("Failed to send HELLO from node %s on port %d: %s",
                myAddress.str().c_str(), localPort, e.what());
    }
}

void DominatingSetAgent::bindSocket()
{
    if (socketBound)
        return;

    try {
        socket.setOutputGate(gate("socketOut"));
        socket.setCallback(this);
        EV_INFO << "Binding UDP socket for node " << myAddress << " on port " << localPort << " (any address for broadcast)" << endl;

        socket.bind(L3Address(), localPort);
        socket.setBroadcast(true);
        socketBound = true;
        EV_INFO << "Socket successfully bound on port " << localPort << " for node " << myAddress << " (broadcast enabled)" << endl;
    }
    catch (const cRuntimeError& e) {
        EV_ERROR << "Failed to bind UDP socket on port " << localPort << ": " << e.what() << endl;
        throw;
    }
}

void DominatingSetAgent::handleStartOperation(LifecycleOperation *operation)
{
    EV_INFO << "DS Agent started for node " << myAddress << endl;
}

void DominatingSetAgent::handleStopOperation(LifecycleOperation *operation)
{
    if (helloTimer && helloTimer->isScheduled())
        cancelEvent(helloTimer);
    if (algorithmTimer && algorithmTimer->isScheduled())
        cancelEvent(algorithmTimer);

    socket.close();
    socketBound = false;

    routingSocket.close();
    routingSocketBound = false;
}

void DominatingSetAgent::runClusteringAlgorithm()
{
    EV_INFO << "Node " << myAddress << " running clustering algorithm..." << endl;

    // Phase 1
    NodeState newState = STATE_WHITE;
    std::set<L3Address> myNeighbors = getMyOneHopNeighbors();

    EV_WARN << "Node " << myAddress << " myNeighbors: " << myNeighbors.size() << endl;

    for (const auto& u : myNeighbors) {
        for (const auto& v : myNeighbors) {
            if (u == v) continue;

            bool u_sees_v = false;
            auto it_u = neighborTable.find(u);
            if (it_u != neighborTable.end()) {
                if (it_u->second.oneHopNeighbors.count(v)) {
                    u_sees_v = true;
                }
            }

            bool v_sees_u = false;
            auto it_v = neighborTable.find(v);
            if (it_v != neighborTable.end()) {
                if (it_v->second.oneHopNeighbors.count(u)) {
                    v_sees_u = true;
                }
            }

            if (!u_sees_v && !v_sees_u) {
                newState = STATE_GRAY;
                break;
            }
        }
        if (newState == STATE_GRAY) break;
    }

    // Phase 2
    if (newState == STATE_GRAY) {
        if (checkRule1()) {
            newState = STATE_WHITE;
            EV_INFO << "Node " << myAddress << " pruned by Rule 1." << endl;
        }
        else if (checkRule2()) {
            newState = STATE_WHITE;
            EV_INFO << "Node " << myAddress << " pruned by Rule 2." << endl;
        }
    }

    if (newState == STATE_GRAY) {
        newState = STATE_BLACK;
    }

    if (newState != STATE_BLACK) {
        bool neighborHasBlack = false;
        std::set<L3Address> neighbors = getMyOneHopNeighbors();
        for (const auto& neighbor : neighbors) {
            auto it = neighborTable.find(neighbor);
            if (it != neighborTable.end() && it->second.state == STATE_BLACK) {
                neighborHasBlack = true;
                break;
            }
        }
        if (!neighborHasBlack && !neighbors.empty()) {
            bool lowestId = true;
            for (const auto& neighbor : neighbors) {
                if (neighbor.toIpv4().getInt() < myId) {
                    lowestId = false;
                    break;
                }
            }
            if (lowestId) {
                newState = STATE_BLACK;
            }
        }
    }

    setState(newState);
}

void DominatingSetAgent::setState(NodeState newState)
{
    bool wasClusterHead = (myState == STATE_BLACK);
    bool isNowClusterHead = (newState == STATE_BLACK);

    if (myState != newState) {
        EV_WARN << "Node " << myAddress << " state change " << myState << " -> " << newState << endl;

        if (wasClusterHead && !isNowClusterHead) {
            if (clusterHeadStartTime >= 0) {
                simtime_t lifetime = simTime() - clusterHeadStartTime;
                totalClusterHeadTime += lifetime;
                clusterHeadStartTime = -1.0;
            }
        } else if (!wasClusterHead && isNowClusterHead) {
            clusterHeadStartTime = simTime();
            timesAsClusterHead++;
        }

        myState = newState;

        // visual
        cModule *host = getContainingNode(this);
        if (host) {
            if (newState == STATE_BLACK) {
                host->getDisplayString().setTagArg("i", 1, "red");
                host->getDisplayString().setTagArg("t", 0, "CH");
            } else if (newState == STATE_GRAY) {
                host->getDisplayString().setTagArg("i", 1, "yellow");
                host->getDisplayString().setTagArg("t", 0, "CANDIDATE");
            } else {
                host->getDisplayString().setTagArg("i", 1, "white");
                host->getDisplayString().setTagArg("t", 0, "");
            }
        }
    }

    emit(amIClusterHeadSignal, (myState == STATE_BLACK) ? 1.0 : 0.0);
}

void DominatingSetAgent::purgeNeighborTable()
{
    for (auto it = neighborTable.begin(); it != neighborTable.end(); ) {
        if (simTime() - it->second.lastHeard > neighborPurgeTimeout) {
            EV_INFO << "Node " << myAddress << " purging neighbor " << it->first << endl;
            it = neighborTable.erase(it);
        } else {
            ++it;
        }
    }
}

bool DominatingSetAgent::isNeighbor(const L3Address& addr) const
{
    return neighborTable.count(addr);
}

std::set<L3Address> DominatingSetAgent::getMyOneHopNeighbors() const
{
    std::set<L3Address> neighbors;
    for (const auto& pair : neighborTable) {
        neighbors.insert(pair.first);
    }
    return neighbors;
}

bool DominatingSetAgent::checkRule1()
{
    std::set<L3Address> myNeighbors = getMyOneHopNeighbors();

    for (const auto& pair : neighborTable) {
        const L3Address& u = pair.first;
        const NeighborInfo& u_info = pair.second;
        int u_id = u.toIpv4().getInt();

        if (u_info.state == STATE_GRAY && u_id > myId) {
            std::set<L3Address> u_coverage = u_info.oneHopNeighbors;
            u_coverage.insert(u);

            if (isCoveredBy(u_coverage)) {
                return true;
            }
        }
    }
    return false;
}

bool DominatingSetAgent::checkRule2()
{
    for (const auto& pair_u : neighborTable) {
        const L3Address& u = pair_u.first;
        const NeighborInfo& u_info = pair_u.second;
        int u_id = u.toIpv4().getInt();

        if (u_info.state != STATE_GRAY || u_id <= myId) continue;

        for (const auto& pair_w : neighborTable) {
            const L3Address& w = pair_w.first;
            const NeighborInfo& w_info = pair_w.second;
            int w_id = w.toIpv4().getInt();

            if (u == w) continue;
            if (w_info.state != STATE_GRAY || w_id <= myId) continue;

            bool adjacent = false;
            if (u_info.oneHopNeighbors.count(w) || w_info.oneHopNeighbors.count(u)) {
                adjacent = true;
            }

            if (adjacent) {
                std::set<L3Address> combined_coverage = getCombinedNeighborSet(u, w);
                if (isCoveredBy(combined_coverage)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool DominatingSetAgent::isCoveredBy(const std::set<L3Address>& coveringSet)
{
    std::set<L3Address> myNeighbors = getMyOneHopNeighbors();
    for (const auto& n : myNeighbors) {
        if (coveringSet.find(n) == coveringSet.end()) {
            return false;
        }
    }
    return true;
}

std::set<L3Address> DominatingSetAgent::getCombinedNeighborSet(const L3Address& u, const L3Address& w)
{
    std::set<L3Address> combined_set;

    combined_set.insert(u);
    auto it_u = neighborTable.find(u);
    if(it_u != neighborTable.end()) {
        combined_set.insert(it_u->second.oneHopNeighbors.begin(), it_u->second.oneHopNeighbors.end());
    }

    combined_set.insert(w);
    auto it_w = neighborTable.find(w);
    if(it_w != neighborTable.end()) {
        combined_set.insert(it_w->second.oneHopNeighbors.begin(), it_w->second.oneHopNeighbors.end());
    }

    return combined_set;
}

void DominatingSetAgent::updateClusterMembership()
{
    L3Address currentClusterHead = findMyClusterHead();

    if (currentClusterHead != myClusterInfo.clusterHead) {
        // Cluster membership changed
        if (myClusterInfo.clusterHead.isUnspecified() == false) {
            myClusterInfo.membershipChanges++;
        }
        myClusterInfo.clusterHead = currentClusterHead;
        myClusterInfo.joinTime = simTime();
    }
}

void DominatingSetAgent::detectGateway()
{
    bool wasGateway = isGateway;
    isGateway = false;

    if (myState != STATE_BLACK) {
        std::set<L3Address> clusterHeads = getClusterHeads();
        if (clusterHeads.size() > 1) {
            isGateway = true;
            // visual
            cModule *host = getContainingNode(this);
            if (host) {
                host->getDisplayString().setTagArg("i", 1, "blue");  // GW
                host->getDisplayString().setTagArg("t", 0, "GW");
            }

        }
    }

    if (wasGateway != isGateway) {

        cModule *host = getContainingNode(this);
        if (host && wasGateway) {
            host->getDisplayString().setTagArg("i", 1, "white");  // duz
            host->getDisplayString().setTagArg("t", 0, "");
        }
    }
}