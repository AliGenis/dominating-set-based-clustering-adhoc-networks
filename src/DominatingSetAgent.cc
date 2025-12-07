 #include "DominatingSetAgent.h"
 #include "inet/common/ModuleAccess.h"
 #include "inet/common/packet/Packet.h"
 #include "inet/networklayer/common/L3AddressTag_m.h"
 #include "inet/common/ProtocolTag_m.h"
 #include "inet/networklayer/contract/IInterfaceTable.h"
 #include "inet/linklayer/common/InterfaceTag_m.h"
 #include "inet/networklayer/ipv4/Ipv4InterfaceData.h"
 #include "inet/networklayer/ipv4/Ipv4Header_m.h"
 #include "inet/common/Units.h"
 #include "inet/transportlayer/common/L4PortTag_m.h"
 #include "inet/networklayer/common/HopLimitTag_m.h"
 #include "inet/networklayer/common/NextHopAddressTag_m.h"
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
 
         registerNetfilterHook();
 
         simtime_t firstHelloTime = simTime() + uniform(0, helloInterval);
         simtime_t firstAlgoTime = simTime() + uniform(0, algorithmInterval);
         scheduleAt(firstHelloTime, helloTimer);
         scheduleAt(firstAlgoTime, algorithmTimer);
         EV_INFO << "Node " << myAddress << " scheduled first HELLO at " << firstHelloTime
                 << ", first algorithm run at " << firstAlgoTime << endl;
 
         computationCostSignal = registerSignal("computationCost");
 
         previousClusterHead = L3Address();
         clusterHeadStartTime = -1.0;
     }
 }
 
 void DominatingSetAgent::registerNetfilterHook()
 {
     if (hookRegistered) return;
 
     cModule *host = getContainingNode(this);
 
     cModule *ipv4Module = nullptr;
 
     cModule *networkLayer = host->getSubmodule("ipv4");
     if (networkLayer) {
         ipv4Module = networkLayer->getSubmodule("ip");
         EV_INFO << "Node " << myAddress << " found ipv4.ip module" << endl;
     }
 
     if (ipv4Module) {
         networkProtocol = dynamic_cast<INetfilter *>(ipv4Module);
         if (networkProtocol) {
             networkProtocol->registerHook(0, this);  // Priority 0
             hookRegistered = true;
             EV_WARN << "*** HOOK REGISTERED: Node " << myAddress << " netfilter hook active for backbone routing ***" << endl;
         } else {
             EV_WARN << "ERROR: Node " << myAddress << " IPv4 module does not implement INetfilter" << endl;
         }
     } else {
         EV_WARN << "ERROR: Node " << myAddress << " could not find IPv4 module for hook registration" << endl;
     }
 }
 
 bool DominatingSetAgent::isControlPacket(const std::string& packetName)
 {
     return packetName.find("DsHello") != std::string::npos ||
         packetName.find("aodv") != std::string::npos ||
         packetName.find("Aodv") != std::string::npos ||
         packetName.find("AODV") != std::string::npos ||
         packetName.find("dsdv") != std::string::npos ||
         packetName.find("DSDV") != std::string::npos ||
         packetName.find("gpsr") != std::string::npos ||
         packetName.find("GPSR") != std::string::npos ||
         packetName.find("arp") != std::string::npos ||
         packetName.find("Arp") != std::string::npos ||
         packetName.find("ARP") != std::string::npos;
 }
 
 INetfilter::IHook::Result DominatingSetAgent::datagramLocalOutHook(Packet *datagram)
 {
     // data packets sent by this node send via CH
     auto networkHeader = datagram->peekAtFront<Ipv4Header>();
     L3Address source = networkHeader->getSrcAddress();
     L3Address destination = networkHeader->getDestAddress();
     std::string packetName = datagram->getName();
 
     if (source == myAddress && !destination.isBroadcast() && !destination.isMulticast() &&
         !isControlPacket(packetName) && !isRoutingPacket(packetName)) {
         packetsSent++;
         bytesSent += datagram->getTotalLength().get();

         long packetId = datagram->getId();
         packetSendTimes[packetId] = simTime();
     }
 
     if (myState == STATE_WHITE) {
         L3Address myCH = findMyClusterHead();
         if (!myCH.isUnspecified()) {
             datagram->removeTagIfPresent<NextHopAddressReq>();
             datagram->addTag<NextHopAddressReq>()->setNextHopAddress(myCH);
         }
     }
     return ACCEPT;
 }
 
 bool DominatingSetAgent::isRoutingPacket(const std::string& packetName)
 {
     return packetName.find("aodv") != std::string::npos ||
         packetName.find("Aodv") != std::string::npos ||
         packetName.find("AODV") != std::string::npos ||
         packetName.find("dsdv") != std::string::npos ||
         packetName.find("DSDV") != std::string::npos ||
         packetName.find("gpsr") != std::string::npos ||
         packetName.find("GPSR") != std::string::npos;
 }
 
 // packets arriving at this node
 INetfilter::IHook::Result DominatingSetAgent::datagramPreRoutingHook(Packet *datagram)
 {
     auto networkHeader = datagram->peekAtFront<Ipv4Header>();
     L3Address source = networkHeader->getSrcAddress();
     L3Address destination = networkHeader->getDestAddress();
 
     if(myState != STATE_BLACK && isRoutingPacket(datagram->getName())) {
         return DROP;
     }
 
     if (destination.isBroadcast() || destination.isMulticast()) {
         return ACCEPT;
     }
 
     if (destination == myAddress) {
         EV_INFO << "*** HOOK PRE_ROUTING: Packet arrived at destination " << myAddress << " ***" << endl;
 
         std::string packetName = datagram->getName();
         if (!isControlPacket(packetName) && !isRoutingPacket(packetName)) {
             packetsReceived++;
             bytesReceived += datagram->getTotalLength().get();
 
             long packetId = datagram->getId();
             auto it = packetSendTimes.find(packetId);
             if (it != packetSendTimes.end()) {
                 simtime_t delay = simTime() - it->second;
                 if (delay >= 0) {
                     totalDelay += delay.dbl();
                 }
                 packetSendTimes.erase(it);
             }
 
             simtime_t cleanupTime = simTime() - 10.0;
             for (auto it = packetSendTimes.begin(); it != packetSendTimes.end(); ) {
                 if (it->second < cleanupTime) {
                     it = packetSendTimes.erase(it);
                 } else {
                     ++it;
                 }
             }
         }
         return ACCEPT;
     }
 
     std::string packetName = datagram->getName();
     if (isControlPacket(packetName)) {
         return ACCEPT;
     }
 
     const char* stateNames[] = {"MEMBER", "GRAY", "CH"};
     EV_INFO << "*** HOOK PRE_ROUTING: " << stateNames[myState] << " " << myAddress
             << " received '" << packetName << "' from " << source << " to " << destination << " ***" << endl;
 
     if (myState != STATE_BLACK) {
         L3Address myCH = findMyClusterHead();
         EV_WARN << "*** HOOK PRE_ROUTING: Member " << myAddress
                     << " redirecting forwarded packet to CH " << myCH
                     << " from " << source << " to " << destination
                     << " (members don't forward) ***" << endl;
         return DROP;
     }
     return ACCEPT;
 }

 INetfilter::IHook::Result DominatingSetAgent::datagramForwardHook(Packet *datagram)
 {
     EV_WARN << "********* !!!!!!!!!!!FORWARD HOOK: Node " << myAddress << " forwarding packet to " << datagram->getName() << " ***" << endl;
     if (myState == STATE_BLACK) {
         auto networkHeader = datagram->peekAtFront<Ipv4Header>();
         L3Address destination = networkHeader->getDestAddress();

         L3Address myCalculatedNextHop = getNextHopForDestination(destination);
 
         if (!myCalculatedNextHop.isUnspecified()) {
              datagram->removeTagIfPresent<NextHopAddressReq>();
              datagram->addTag<NextHopAddressReq>()->setNextHopAddress(myCalculatedNextHop);
 
              EV_INFO << "CH Override: Forcing packet to backbone neighbor: " << myCalculatedNextHop << endl;
         }
     }
 
     return ACCEPT;
 }
 
 INetfilter::IHook::Result DominatingSetAgent::datagramPostRoutingHook(Packet *datagram)
 {
     auto networkHeader = datagram->peekAtFront<Ipv4Header>();
     L3Address destination = networkHeader->getDestAddress();
     L3Address source = networkHeader->getSrcAddress();
     if (source != myAddress && myState != STATE_BLACK) return DROP;
 
     if (destination.isBroadcast() || destination.isMulticast() || destination == myAddress) {
         return ACCEPT;
     }
 
     std::string packetName = datagram->getName();
     if (isControlPacket(packetName)) {
         return ACCEPT;
     }
 
     auto nextHopTag = datagram->findTag<NextHopAddressReq>();
     if (!nextHopTag) {
         EV_ERROR << "*** POST_ROUTING: No next hop set yet, allowing protocol route ***" << endl;
         return ACCEPT;
     }
 
     L3Address protocolNextHop = nextHopTag->getNextHopAddress();
 
     if (protocolNextHop == destination) {
         if (isNeighbor(destination)) {
             auto destIt = neighborTable.find(destination);
             if (destIt != neighborTable.end() && destIt->second.state != STATE_BLACK) {
                 return ACCEPT;
             }
         }
     }
 
     if (!protocolNextHop.isUnspecified() && protocolNextHop != destination) {
         auto nextHopIt = neighborTable.find(protocolNextHop);
         if (nextHopIt != neighborTable.end()) {
             bool isBackboneNode = (nextHopIt->second.state == STATE_BLACK);
             if (!isBackboneNode) {
                 EV_INFO << "*** POST_ROUTING: Routing protocol chose non-backbone " << protocolNextHop
                         << ", overriding to use CH/Gateway backbone ***" << endl;
 
                 L3Address bestBackbone;
 
                 if (bestBackbone.isUnspecified()) {
                     for (const auto& pair : neighborTable) {
                         if (pair.second.state == STATE_BLACK) {
                             bestBackbone = pair.first;
                             break;
                         }
                     }
                 }
 
                 if (!bestBackbone.isUnspecified()) {
                     EV_INFO << "*** POST_ROUTING: Redirecting to backbone node " << bestBackbone
                             << " instead of non-backbone " << protocolNextHop << " ***" << endl;
                     datagram->removeTag<NextHopAddressReq>();
                     datagram->addTag<NextHopAddressReq>()->setNextHopAddress(bestBackbone);
                 } else {
                     EV_ERROR << "*** POST_ROUTING: No backbone node (CH/Gateway) found, allowing protocol route ***" << endl;
                 }
             } else {
                 EV_DEBUG << "*** POST_ROUTING: Routing protocol chose backbone node " << protocolNextHop
                          << " - OK ***" << endl;
             }
         } else {
             EV_DEBUG << "*** POST_ROUTING: Next hop " << protocolNextHop
                      << " not in neighbor table - allowing protocol route ***" << endl;
         }
     }
 
     return ACCEPT;
 }
 
 L3Address DominatingSetAgent::getNextHopForDestination(const L3Address& destination)
 {
     const char* stateNames[] = {"MEMBER", "GRAY", "CH"};
     EV_DEBUG << "ROUTING: Node " << myAddress << " (" << stateNames[myState]
             << ") finding next hop to " << destination << endl;

     if (myState != STATE_BLACK) {
         L3Address myCH = findMyClusterHead();
 
         if (myCH.isUnspecified()) {
             EV_ERROR << "*** ROUTING ERROR: Member " << myAddress << " has no cluster head! ***" << endl;
             return L3Address();
         }
         return myCH;
     }
     else if(myState==STATE_BLACK) {
         if (isNeighbor(destination)) {
             auto destIt = neighborTable.find(destination);
             if (destIt != neighborTable.end()) {
                 EV_INFO << "*** ROUTING: CH " << myAddress << " -> Member " << destination
                         << " (final delivery to my member) ***" << endl;
                 return destination;
             }
         }
     }
     return L3Address();
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
             lastAlgorithmStartTime = simTime();
             runClusteringAlgorithm();
             updateClusterMembership();
             updateNodeVisualization();
             trackComputationCost();
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
 
     const char* stateNames[] = {"WHITE", "GRAY", "BLACK"};
     EV_DEBUG << "*** DS-HELLO RECEIVED: Node " << myAddress << " got HELLO from " << sender
             << " (state=" << stateNames[payload->getState()] << ") ***" << endl;
 
     NeighborInfo& info = neighborTable[sender];
     info.lastHeard = simTime();
     info.state = (NodeState)payload->getState();
 
     info.oneHopNeighbors.clear();
     for (unsigned int i = 0; i < payload->getNeighborsArraySize(); i++) {
         L3Address neighborAddr;
         if (L3AddressResolver().tryResolve(payload->getNeighbors(i), neighborAddr)) {
             info.oneHopNeighbors.insert(neighborAddr);
         }
     }
 
     delete packet;
 
     helloReceivedCount++;
 }
 
 void DominatingSetAgent::finish()
 {
     ApplicationBase::finish();
 }
 
 void DominatingSetAgent::broadcastHello()
 {
     if (!socketBound) {
         EV_WARN << "Socket not bound when attempting to broadcast. Binding now..." << endl;
         bindSocket();
     }
 
     EV_INFO << "*** DS-HELLO: Node " << myAddress << " broadcasting HELLO (State: " << myState
             << ", neighbors: " << neighborTable.size() << ") ***" << endl;
 
     auto payload = makeShared<DsHelloPacket>();
     payload->setSenderAddress(myAddress.str().c_str());
     payload->setState(myState);
 
     std::set<L3Address> neighbors = getMyOneHopNeighbors();
     payload->setNeighborsArraySize(neighbors.size());
     int i = 0;
     for (const auto& addr : neighbors) {
         payload->setNeighbors(i++, addr.str().c_str());
     }
 
     payload->setChunkLength(B(32 + 4 * neighborTable.size()));
 
     controlOverheadCount++;
     Packet *packet = new Packet("DsHello", payload);
 
     if (wirelessInterfaceId >= 0) {
         packet->addTag<InterfaceReq>()->setInterfaceId(wirelessInterfaceId);
     }

     L3Address destAddr = Ipv4Address::ALLONES_ADDRESS;
     packet->addTag<L3AddressReq>()->setDestAddress(destAddr);
     packet->addTag<L4PortReq>()->setDestPort(destPort);
 
     try {
         socket.send(packet);
     }
     catch (const cRuntimeError& e) {
         delete packet;
         EV_ERROR << "Failed to send HELLO from node " << myAddress << ": " << e.what() << endl;
     }
 }
 
 void DominatingSetAgent::bindSocket()
 {
     if (socketBound)
         return;
 
     try {
         socket.setOutputGate(gate("socketOut"));
         socket.setCallback(this);
         socket.bind(L3Address(), localPort);
         socket.setBroadcast(true);
         socketBound = true;
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
 }
 
 void DominatingSetAgent::runClusteringAlgorithm()
 {
     std::set<L3Address> myNeighbors = getMyOneHopNeighbors();
 
     int blackNeighbors = 0, grayNeighbors = 0, whiteNeighbors = 0;
     for (const auto& pair : neighborTable) {
         if (pair.second.state == STATE_BLACK) blackNeighbors++;
         else if (pair.second.state == STATE_GRAY) grayNeighbors++;
         else whiteNeighbors++;
     }
 
     double warmupTime = 3 * helloInterval;
     if (simTime() < warmupTime) {
         EV_WARN << "*** ALGO: Node " << myAddress << " in warmup period, not making CH decisions yet ***" << endl;
         return;
     }
 
     EV_DEBUG << "ALGO: Node " << myAddress << " running algorithm. Neighbors: "
             << myNeighbors.size() << " (BLACK:" << blackNeighbors
             << " GRAY:" << grayNeighbors << " WHITE:" << whiteNeighbors << ")" << endl;
 
     NodeState newState = STATE_WHITE;
 
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
         /*TODO: I think this is not valid in this design
         bool lowerIdCandidate = false;
         for (const auto& neighbor : myNeighbors) {
             if (neighbor.toIpv4().getInt() < myId) {
                 auto it = neighborTable.find(neighbor);
                 if (it != neighborTable.end() &&
                     (it->second.state == STATE_GRAY || it->second.state == STATE_BLACK)) {
                     lowerIdCandidate = true;
                     break;
                 }
             }
         }
 
         if (lowerIdCandidate) {
             EV_INFO << "Node " << myAddress << " deferring to lower-ID candidate neighbor" << endl;
             newState = STATE_WHITE;
         } else {
             newState = STATE_BLACK;
         }
         */
     }
 
     if (newState != STATE_BLACK) {
         bool neighborHasBlack = false;
         L3Address blackNeighborAddr;
         std::set<L3Address> neighbors = getMyOneHopNeighbors();
         for (const auto& neighbor : neighbors) {
             auto it = neighborTable.find(neighbor);
             if (it != neighborTable.end() && it->second.state == STATE_BLACK) {
                 neighborHasBlack = true;
                 blackNeighborAddr = neighbor;
                 break;
             }
         }
 
         if (neighborHasBlack) {
             EV_WARN << "*** ALGO: Node " << myAddress << " has BLACK neighbor "
                     << blackNeighborAddr << ", staying WHITE ***" << endl;
         } else if (!neighbors.empty()) {
             bool lowestId = true;
             L3Address lowerNeighbor;
             for (const auto& neighbor : neighbors) {
                 if (neighbor.toIpv4().getInt() < myId) {
                     lowestId = false;
                     lowerNeighbor = neighbor;
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
         const char* stateNames[] = {"WHITE", "GRAY", "BLACK"};
         EV_WARN << "*** STATE CHANGE: Node " << myAddress << " "
                 << stateNames[myState] << " -> " << stateNames[newState]
                 << (isNowClusterHead ? " [CLUSTER HEAD]" : "") << " ***" << endl;
 
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
 
         trackReclustering();
     }
 }
 
 void DominatingSetAgent::updateNodeVisualization()
 {
     cModule *host = getContainingNode(this);
     if (!host) return;
 
     static std::map<L3Address, std::string> lastVisualState;
     std::string currentVisual;
 
     if (myState == STATE_BLACK) {
         host->getDisplayString().setTagArg("i", 1, "red");
         host->getDisplayString().setTagArg("t", 0, "CH");
         currentVisual = "CH";
     } else if (myState == STATE_GRAY) {
         host->getDisplayString().setTagArg("i", 1, "yellow");
         host->getDisplayString().setTagArg("t", 0, "CAND");
         currentVisual = "CAND";
     } else {
         host->getDisplayString().setTagArg("i", 1, "white");
         host->getDisplayString().setTagArg("t", 0, "");
         currentVisual = "MEMBER";
     }

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
     for (const auto& pair : neighborTable) {
         const L3Address& u = pair.first;
         const NeighborInfo& u_info = pair.second;
         int u_id = u.toIpv4().getInt();
 
         if ((u_info.state == STATE_GRAY || u_info.state == STATE_BLACK) && u_id > myId) {
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
 
         if ((u_info.state != STATE_GRAY && u_info.state != STATE_BLACK) || u_id <= myId) continue;
 
         for (const auto& pair_w : neighborTable) {
             const L3Address& w = pair_w.first;
             const NeighborInfo& w_info = pair_w.second;
             int w_id = w.toIpv4().getInt();
 
             if (u == w) continue;
             if ((w_info.state != STATE_GRAY && w_info.state != STATE_BLACK) || w_id <= myId) continue;
 
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
         if (!myClusterInfo.clusterHead.isUnspecified()) {
             myClusterInfo.membershipChanges++;
         }
         myClusterInfo.clusterHead = currentClusterHead;
         myClusterInfo.joinTime = simTime();
   }
 }
 
 double DominatingSetAgent::getEnergyConsumption() const
 {
     try {
         cModule *host = getContainingNode(const_cast<DominatingSetAgent*>(this));
         if (!host) return 0.0;
 
         cModule *energyStorage = host->getSubmodule("energyStorage");
         if (!energyStorage) {
             cModule *wlanModule = host->getSubmodule("wlan", 0);
             if (wlanModule) {
                 cModule *radioModule = wlanModule->getSubmodule("radio");
                 if (radioModule) {
                     energyStorage = radioModule->getSubmodule("energyStorage");
                 }
             }
         }
 
         if (!energyStorage) return 0.0;
 
         double initial = 0.0;
         double current = 0.0;
 
         if (energyStorage->hasPar("initialCapacity")) {
             initial = energyStorage->par("initialCapacity").doubleValue();
         } else if (energyStorage->hasPar("nominalCapacity")) {
             initial = energyStorage->par("nominalCapacity").doubleValue();
         }
 
         if (energyStorage->hasPar("capacity")) {
             current = energyStorage->par("capacity").doubleValue();
         } else if (energyStorage->hasPar("residualCapacity")) {
             current = energyStorage->par("residualCapacity").doubleValue();
         }
 
         double consumed = initial - current;
         return (consumed > 0) ? consumed : 0.0;
 
     } catch (...) {
         return 0.0;
     }
 }
 
 bool DominatingSetAgent::isNodeAlive() const
 {
     try {
         cModule *host = getContainingNode(const_cast<DominatingSetAgent*>(this));
         if (!host) return true;
 
         cModule *energyStorage = host->getSubmodule("energyStorage");
         if (!energyStorage) {
             cModule *wlanModule = host->getSubmodule("wlan", 0);
             if (wlanModule) {
                 cModule *radioModule = wlanModule->getSubmodule("radio");
                 if (radioModule) {
                     energyStorage = radioModule->getSubmodule("energyStorage");
                 }
             }
         }
 
         if (!energyStorage) return true;
 
         double current = 0.0;
         if (energyStorage->hasPar("capacity")) {
             current = energyStorage->par("capacity").doubleValue();
         } else if (energyStorage->hasPar("residualCapacity")) {
             current = energyStorage->par("residualCapacity").doubleValue();
         }
 
         return current > 0.0;
 
     } catch (...) {
         return true;
     }
 }
 
 int DominatingSetAgent::calculateClusterSize() const
 {
     L3Address myCH = findMyClusterHead();
     if (myCH.isUnspecified()) {
         return 1;
     }
 
     int size = 1;
     for (const auto& pair : neighborTable) {
         const L3Address& neighbor = pair.first;
         const NeighborInfo& info = pair.second;
 
         if (neighbor == myCH || (myState == STATE_BLACK && info.state != STATE_BLACK)) {
             size++;
         }
     }
     return size;
 }
 
 void DominatingSetAgent::trackComputationCost()
 {
     if (lastAlgorithmStartTime > 0) {
         simtime_t computationTime = simTime() - lastAlgorithmStartTime;
         emit(computationCostSignal, computationTime.dbl());
     }
 }
 
 void DominatingSetAgent::trackReclustering()
 {
     simtime_t timeSinceLastReclustering = simTime() - lastReclusteringTime;
 
     if (previousClusterHead != myClusterInfo.clusterHead &&
         timeSinceLastReclustering > algorithmInterval) {
         reclusteringCount++;
         lastReclusteringTime = simTime();
     }
     previousClusterHead = myClusterInfo.clusterHead;
 }
 
 std::set<L3Address> DominatingSetAgent::getClusterHeads() const
 {
     std::set<L3Address> clusterHeads;
     for (const auto& pair : neighborTable) {
         if (pair.second.state == STATE_BLACK) {
             clusterHeads.insert(pair.first);
         }
     }
     return clusterHeads;
 }
 
 L3Address DominatingSetAgent::findMyClusterHead() const
 {
     if (myState == STATE_BLACK) {
         return myAddress;
     }
 
     L3Address nearestCH;
     int minId = INT_MAX;
 
     for (const auto& pair : neighborTable) {
         if (pair.second.state == STATE_BLACK) {
             int chId = pair.first.toIpv4().getInt();
             if (chId < minId) {
                 minId = chId;
                 nearestCH = pair.first;
             }
         }
     }
 
     return nearestCH;
 }
 
 simtime_t DominatingSetAgent::getCurrentClusterHeadLifetime() const
 {
     if (myState == STATE_BLACK && clusterHeadStartTime >= 0) {
         return simTime() - clusterHeadStartTime;
     }
     return 0.0;
 }
 
 double DominatingSetAgent::getThroughput() const
 {
     if (simTime() > 0) {
         return bytesReceived / simTime().dbl();
     }
     return 0.0;
 }
 
 double DominatingSetAgent::getAverageDelay() const
 {
     if (packetsReceived > 0) {
         return totalDelay / packetsReceived;
     }
     return 0.0;
 }
 
 double DominatingSetAgent::getPacketDeliveryRatio() const
 {
     if (packetsSent > 0) {
         return static_cast<double>(packetsReceived) / packetsSent;
     }
     return 0.0;
 }
 