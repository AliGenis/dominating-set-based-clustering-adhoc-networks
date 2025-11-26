/*
 * DominatingSetAgent.h
 *
 * Header file for the Dominating Set (Wu & Li) clustering agent.
 * Compatible with INET 4.5
 */

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
#include "inet/common/ModuleAccess.h"
#include "inet/common/packet/Packet.h"
#include "inet/common/geometry/common/Coord.h"
#include "inet/mobility/contract/IMobility.h"

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
    Coord position;
};

struct ClusterInfo {
    L3Address clusterHead;
    simtime_t joinTime;
    int membershipChanges = 0;
};

class DominatingSetAgent : public ApplicationBase, public UdpSocket::ICallback
{
  protected:
    UdpSocket socket;
    bool socketBound = false;
    bool routingSocketBound = false;

    cMessage *helloTimer = nullptr;
    cMessage *algorithmTimer = nullptr;

    NodeState myState = STATE_WHITE;
    L3Address myAddress;
    int myId = 0;
    bool isGateway = false;
    NetworkInterface *wirelessInterface = nullptr;
    int wirelessInterfaceId = -1;
    L3Address broadcastAddress;

    std::map<L3Address, NeighborInfo> neighborTable;

    ClusterInfo myClusterInfo;
    L3Address previousClusterHead;
    simtime_t clusterHeadStartTime;
    int timesAsClusterHead = 0;
    simtime_t totalClusterHeadTime = 0.0;

    simtime_t lastReclusteringTime = 0.0;
    int reclusteringCount = 0;

    simtime_t lastAlgorithmStartTime = 0.0;

    double helloInterval = 1.0;
    double algorithmInterval = 5.0;
    double neighborPurgeTimeout = 10.0;

    int localPort = 5000;
    int destPort = 5000;

    long helloReceivedCount = 0;

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

    virtual void broadcastHello();
    virtual void runClusteringAlgorithm();
    virtual void purgeNeighborTable();
    virtual void setState(NodeState newState);

    virtual void updateClusterMembership();
    virtual void detectGateway();

    virtual void bindSocket();
    virtual bool isNeighbor(const L3Address& addr) const;
    virtual std::set<L3Address> getMyOneHopNeighbors() const;
    virtual std::set<L3Address> getClusterHeads() const;
    virtual L3Address findMyClusterHead() const;
    virtual bool checkRule1();
    virtual bool checkRule2();
    virtual std::set<L3Address> getCombinedNeighborSet(const L3Address& u, const L3Address& w);
    virtual bool isCoveredBy(const std::set<L3Address>& coveringSet);

  public:
    DominatingSetAgent() {}
    virtual ~DominatingSetAgent();

    virtual bool isDominatingNode() const {
        return (myState == STATE_BLACK || isGateway);
    }
};

#endif // DOMINATINGSETAGENT_H_
