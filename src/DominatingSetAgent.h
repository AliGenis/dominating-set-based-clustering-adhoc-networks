 #ifndef DOMINATINGSETAGENT_H_
 #define DOMINATINGSETAGENT_H_
 
 #include <map>
 #include <set>
 #include "inet/common/INETDefs.h"
 #include "inet/applications/base/ApplicationBase.h"
 #include "inet/transportlayer/contract/udp/UdpSocket.h"
 #include "inet/networklayer/common/L3Address.h"
 #include "inet/networklayer/common/L3AddressResolver.h"
 #include "inet/networklayer/contract/IInterfaceTable.h"
 #include "inet/networklayer/common/NetworkInterface.h"
 #include "inet/networklayer/contract/INetfilter.h"
 #include "inet/networklayer/ipv4/Ipv4.h"
 #include "inet/common/ModuleAccess.h"
 #include "inet/common/packet/Packet.h"
 #include "inet/common/geometry/common/Coord.h"
 #include "inet/mobility/contract/IMobility.h"
 #include <chrono>
 
 #include "DsAgentPacket_m.h"
 
 using namespace inet;
 using namespace src;
 
 enum NodeState {
     STATE_WHITE = 0,  // Plain
     STATE_GRAY = 1,   // Candidate CH
     STATE_BLACK = 2   // CH
 };
 
 struct NeighborInfo {
     simtime_t lastHeard;
     NodeState state = STATE_WHITE;
     std::set<L3Address> oneHopNeighbors;
 };
 
 struct ClusterInfo {
     L3Address clusterHead;
     simtime_t joinTime;
     int membershipChanges = 0;
 };
 
 class DominatingSetAgent : public ApplicationBase, public UdpSocket::ICallback, public NetfilterBase::HookBase
 {
   protected:
     UdpSocket socket;
     bool socketBound = false;
 
     cMessage *helloTimer = nullptr;
     cMessage *algorithmTimer = nullptr;
 
     NodeState myState = STATE_WHITE;
     L3Address myAddress;
     int myId = 0;
     NetworkInterface *wirelessInterface = nullptr;
     int wirelessInterfaceId = -1;
     L3Address broadcastAddress;
 
     INetfilter *networkProtocol = nullptr;
     bool hookRegistered = false;
 
     std::map<L3Address, NeighborInfo> neighborTable;
     
     ClusterInfo myClusterInfo;
     L3Address previousClusterHead;
     simtime_t clusterHeadStartTime;
     int timesAsClusterHead = 0;
     simtime_t totalClusterHeadTime = 0.0;
     
     simtime_t lastReclusteringTime = 0.0;
     int reclusteringCount = 0;
     
     simtime_t lastAlgorithmStartTime = 0.0;
     std::chrono::high_resolution_clock::time_point algorithmStartWallTime;
     double totalComputationTime = 0.0;
 
     double helloInterval = 1.0;
     double algorithmInterval = 5.0;
     double neighborPurgeTimeout = 10.0;
 
     int localPort = 5000;
     int destPort = 5000;
 
     simsignal_t computationCostSignal;
     simsignal_t energyConsumptionSignal;
     simsignal_t endToEndDelaySignal;
     
     long helloReceivedCount = 0;
     long backboneRoutedCount = 0;
     long controlOverheadCount = 0;
     
     long packetsSent = 0;
     long packetsReceived = 0;
     long bytesSent = 0;
     long bytesReceived = 0;
     double totalDelay = 0.0;
     std::map<long, simtime_t> packetSendTimes;
 
   protected:
     // ApplicationBase methods
     virtual int numInitStages() const override { return NUM_INIT_STAGES; }
     virtual void initialize(int stage) override;
     virtual void handleMessageWhenUp(cMessage *msg) override;
     virtual void handleStartOperation(LifecycleOperation *operation) override;
     virtual void handleStopOperation(LifecycleOperation *operation) override;
     virtual void handleCrashOperation(LifecycleOperation *operation) override {}
     virtual void finish() override;
 
     // UdpSocket::ICallback methods
     virtual void socketDataArrived(UdpSocket *socket, Packet *packet) override;
     virtual void socketErrorArrived(UdpSocket *socket, Indication *indication) override {}
     virtual void socketClosed(UdpSocket *socket) override {}
 
     // INetfilter::IHook methods
     virtual Result datagramPreRoutingHook(Packet *datagram) override;
     virtual Result datagramForwardHook(Packet *datagram) override;// { return ACCEPT; }
     virtual Result datagramPostRoutingHook(Packet *datagram) override; //{ return ACCEPT; }
     virtual Result datagramLocalInHook(Packet *datagram) override { return ACCEPT; }
     virtual Result datagramLocalOutHook(Packet *datagram) override;
 
     virtual void broadcastHello();
     virtual void runClusteringAlgorithm();
     virtual void purgeNeighborTable();
     virtual void setState(NodeState newState);
     virtual void updateNodeVisualization();
     
     virtual void updateClusterMembership();
     virtual int calculateClusterSize() const;
     virtual void trackComputationCost();
     virtual void trackReclustering();
 
     virtual L3Address getNextHopForDestination(const L3Address& destination);
     virtual void bindSocket();
     virtual void registerNetfilterHook();
     virtual bool isNeighbor(const L3Address& addr) const;
     virtual std::set<L3Address> getMyOneHopNeighbors() const;
     virtual L3Address findMyClusterHead() const;
     virtual bool checkRule1();
     virtual bool checkRule2();
     virtual bool isRoutingPacket(const std::string& packetName);
     virtual bool isControlPacket(const std::string& packetName);
     virtual std::set<L3Address> getCombinedNeighborSet(const L3Address& u, const L3Address& w);
     virtual bool isCoveredBy(const std::set<L3Address>& coveringSet);
 
   public:
     DominatingSetAgent() {}
     virtual ~DominatingSetAgent();
     
     // For observer
     NodeState getState() const { return myState; }
     bool amIClusterHead() const { return myState == STATE_BLACK; }
     L3Address getMyClusterHead() const { return findMyClusterHead(); }
     
     simtime_t getTotalClusterHeadTime() const { return totalClusterHeadTime; }
     int getMembershipChanges() const { return myClusterInfo.membershipChanges; }
     int getReclusteringCount() const { return reclusteringCount; }
     long getControlOverheadCount() const { return controlOverheadCount; }
     L3Address getMyAddress() const { return myAddress; }
     int getClusterSize() const { return calculateClusterSize(); }
     simtime_t getCurrentClusterHeadLifetime() const;
     
     long getPacketsSent() const { return packetsSent; }
     long getPacketsReceived() const { return packetsReceived; }
     double getThroughput() const;
     double getAverageDelay() const;
     double getPacketDeliveryRatio() const;
     
     double getEnergyConsumption() const;
     bool isNodeAlive() const;
     
     std::set<L3Address> getClusterHeads() const;
 };
 
 #endif // DOMINATINGSETAGENT_H_
 