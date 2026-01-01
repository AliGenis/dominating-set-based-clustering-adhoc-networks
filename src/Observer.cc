#include <omnetpp.h>
#include "DominatingSetAgent.h"
#include <set>
#include <map>
#include <vector>
#include <cmath>
#include <cstring>

using namespace omnetpp;
using namespace inet;

class Observer : public cSimpleModule {
  private:
    simsignal_t globalCHSignal;
    
    simsignal_t globalClusterHeadFairnessSignal;
    
    simsignal_t globalClusterCountSignal;
    simsignal_t globalClusterSizeMeanSignal;
    simsignal_t globalClusterStabilityAvgTimeSignal;
    simsignal_t globalClusterHeadLifetimeMeanSignal;
    
    simsignal_t globalControlOverheadSignal;
    simsignal_t globalReclusteringFrequencySignal;
    
    simsignal_t globalThroughputSignal;
    simsignal_t globalEndToEndDelaySignal;
    simsignal_t globalPacketDeliveryRatioSignal;
    
    simsignal_t globalEnergyConsumptionVarianceSignal;
    simsignal_t globalEnergyConsumptionSignal;
    simsignal_t globalLoadBalanceSignal;
    simsignal_t globalNetworkLifetimeSignal;
    
    cMessage *timerMsg;
    double interval;
    simtime_t startTime;
    
    std::map<cModule*, L3Address> previousClusterHeads;
    std::map<cModule*, simtime_t> lastMembershipChangeTime;
    std::map<cModule*, simtime_t> totalStableTime;
    
    simtime_t firstNodeDeathTime;
    bool firstNodeDied;

  public:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
};

Define_Module(Observer);

void Observer::initialize() {
    globalCHSignal = registerSignal("globalCHCount");
    globalClusterHeadFairnessSignal = registerSignal("globalClusterHeadFairness");
    
    globalClusterCountSignal = registerSignal("globalClusterCount");
    globalClusterSizeMeanSignal = registerSignal("globalClusterSizeMean");
    globalClusterStabilityAvgTimeSignal = registerSignal("globalClusterStabilityAvgTime");
    globalClusterHeadLifetimeMeanSignal = registerSignal("globalClusterHeadLifetimeMean");
    
    globalControlOverheadSignal = registerSignal("globalControlOverhead");
    globalReclusteringFrequencySignal = registerSignal("globalReclusteringFrequency");
    
    globalThroughputSignal = registerSignal("globalThroughput");
    globalEndToEndDelaySignal = registerSignal("globalEndToEndDelay");
    globalPacketDeliveryRatioSignal = registerSignal("globalPacketDeliveryRatio");
    
    globalEnergyConsumptionVarianceSignal = registerSignal("globalEnergyConsumptionVariance");
    globalEnergyConsumptionSignal = registerSignal("globalEnergyConsumption");
    globalLoadBalanceSignal = registerSignal("globalLoadBalance");
    globalNetworkLifetimeSignal = registerSignal("globalNetworkLifetime");
    
    interval = par("interval");
    startTime = par("startTime");
    
    firstNodeDeathTime = -1.0;
    firstNodeDied = false;
    
    timerMsg = new cMessage("measureTimer");
    scheduleAt(simTime() + interval, timerMsg);
}

void Observer::handleMessage(cMessage *msg) {
    if (msg == timerMsg) {
        if (simTime() < startTime) {
            scheduleAt(simTime() + interval, timerMsg);
            return;
        }
        cModule *network = getParentModule();
        
        int chCounter = 0;
        std::set<L3Address> allClusterHeads;
        long totalStabilityChanges = 0;
        double totalClusterHeadTime = 0.0;
        long totalControlOverhead = 0;
        long totalReclusteringCount = 0;
        int nodeCount = 0;
        
        std::vector<int> clusterSizes;
        
        std::vector<double> clusterHeadLifetimes;
        
        long nodesWithStableMembership = 0;
        
        for (cModule::SubmoduleIterator it(network); !it.end(); ++it) {
            cModule *sub = *it;
            
            const char *subName = sub->getName();
            if (subName == nullptr || strncmp(subName, "host", 4) != 0) {
                continue;
            }
            
            if (sub->hasSubmoduleVector("app")) {
                cModule *appMod = sub->getSubmodule("app", 0);
                
                if (appMod) {
                    DominatingSetAgent *agent = dynamic_cast<DominatingSetAgent*>(appMod);
                    if (agent) {
                        nodeCount++;
                        
                        if (agent->amIClusterHead()) {
                            chCounter++;
                            L3Address nodeAddr = agent->getMyAddress();
                            if (!nodeAddr.isUnspecified()) {
                                allClusterHeads.insert(nodeAddr);
                            }
                        }
                        
                        std::set<L3Address> nodeCHs = agent->getClusterHeads();
                        allClusterHeads.insert(nodeCHs.begin(), nodeCHs.end());
                        
                        L3Address currentCH = agent->getMyClusterHead();
                        bool membershipChanged = false;
                        if (previousClusterHeads.find(sub) != previousClusterHeads.end()) {
                            if (previousClusterHeads[sub] != currentCH) {
                                totalStabilityChanges++;
                                membershipChanged = true;

                                if (lastMembershipChangeTime.find(sub) != lastMembershipChangeTime.end()) {
                                    simtime_t stableDuration = simTime() - lastMembershipChangeTime[sub];
                                    totalStableTime[sub] += stableDuration;
                                }
                                lastMembershipChangeTime[sub] = simTime();
                            } else {
                                nodesWithStableMembership++;
                            }
                        } else {
                            lastMembershipChangeTime[sub] = simTime();
                            nodesWithStableMembership++;
                        }
                        previousClusterHeads[sub] = currentCH;
                        
                        
                        
                        if (agent->amIClusterHead()) {
                            simtime_t currentLifetime = agent->getCurrentClusterHeadLifetime();
                            if (currentLifetime > 0) {
                                clusterHeadLifetimes.push_back(currentLifetime.dbl());
                            }
                            int clusterSize = agent->getClusterSize();
                            clusterSizes.push_back(clusterSize);
                        }
                        
                        simtime_t chTime = agent->getTotalClusterHeadTime();
                        if (simTime() > 0) {
                            totalClusterHeadTime += chTime.dbl() / simTime().dbl();
                        }
                        
                        totalControlOverhead += agent->getControlOverheadCount();
                        
                        totalReclusteringCount += agent->getReclusteringCount();
                    }
                }
            }
        }
        
        int globalClusterCount = allClusterHeads.size();
        if (globalClusterCount == 0 && chCounter > 0) {
            globalClusterCount = chCounter;
        }
        
        double avgFairness = (nodeCount > 0) ? (totalClusterHeadTime / nodeCount) : 0.0;
        double avgControlOverhead = (nodeCount > 0) ? (static_cast<double>(totalControlOverhead) / nodeCount) : 0.0;
        double avgReclusteringFrequency = (nodeCount > 0) ? (static_cast<double>(totalReclusteringCount) / nodeCount) : 0.0;

        double totalThroughput = 0.0;
        double totalDelay = 0.0;
        long globalPacketsSent = 0;
        long globalPacketsReceived = 0;
        int nodesWithData = 0;
        
        
        for (cModule::SubmoduleIterator it(network); !it.end(); ++it) {
            cModule *sub = *it;
            const char *subName = sub->getName();
            if (subName == nullptr || strncmp(subName, "host", 4) != 0) {
                continue;
            }
            if (sub->hasSubmoduleVector("app")) {
                cModule *appMod = sub->getSubmodule("app", 0);
                if (appMod) {
                    DominatingSetAgent *agent = dynamic_cast<DominatingSetAgent*>(appMod);
                    if (agent) {
                        double throughput = agent->getThroughput();
                        double delay = agent->getAverageDelay();
                        
                        globalPacketsSent += agent->getPacketsSent();
                        globalPacketsReceived += agent->getPacketsReceived();
                        
                        if (throughput > 0 || delay > 0) {
                            totalThroughput += throughput;
                            totalDelay += delay;
                            nodesWithData++;
                        }
                    }
                }
            }
        }
        
        double avgThroughput = (nodesWithData > 0) ? (totalThroughput / nodesWithData) : 0.0;
        double avgDelay = (nodesWithData > 0) ? (totalDelay / nodesWithData) : 0.0;
        double avgPDR = (globalPacketsSent > 0) ? (static_cast<double>(globalPacketsReceived) / globalPacketsSent) : 0.0;
        
        double totalEnergy = 0.0;
        int aliveNodes = 0;
        int deadNodes = 0;
        std::vector<double> energyValues;
        
        for (cModule::SubmoduleIterator it(network); !it.end(); ++it) {
            cModule *sub = *it;
            const char *subName = sub->getName();
            if (subName == nullptr || strncmp(subName, "host", 4) != 0) {
                continue;
            }
            if (sub->hasSubmoduleVector("app")) {
                cModule *appMod = sub->getSubmodule("app", 0);
                if (appMod) {
                    DominatingSetAgent *agent = dynamic_cast<DominatingSetAgent*>(appMod);
                    if (agent) {
                        double energy = agent->getEnergyConsumption();
                        energyValues.push_back(energy);
                        totalEnergy += energy;
                        
                        if (!agent->isNodeAlive()) {
                            deadNodes++;
                            if (!firstNodeDied) {
                                firstNodeDeathTime = simTime();
                                firstNodeDied = true;
                            }
                        } else {
                            aliveNodes++;
                        }
                    }
                }
            }
        }
        double avgEnergy = (nodeCount > 0) ? (totalEnergy / nodeCount) : 0.0;
        
        double energyVariance = 0.0;
        if (!energyValues.empty() && energyValues.size() > 1) {
            double sumSquaredDiff = 0.0;
            for (double energy : energyValues) {
                double diff = energy - avgEnergy;
                sumSquaredDiff += diff * diff;
            }
            energyVariance = sumSquaredDiff / (energyValues.size() - 1);
        }
        
        // Lower is better
        double loadBalance = 0.0;
        if (!energyValues.empty() && avgEnergy > 0) {
            double stdDev = sqrt(energyVariance);
            loadBalance = (avgEnergy > 0) ? (stdDev / avgEnergy) : 0.0;
        }
        
        double networkLifetime = firstNodeDied ? firstNodeDeathTime.dbl() : simTime().dbl();
        
        double clusterSizeMean = 0.0;
        if (!clusterSizes.empty()) {
            double sum = 0.0;
            for (int size : clusterSizes) {
                sum += size;
            }
            clusterSizeMean = sum / clusterSizes.size();
        }
        
        double avgStableTime = 0.0;
        if (totalStabilityChanges > 0 && nodeCount > 0) {
            double totalStableTimeSum = 0.0;
            for (const auto& pair : totalStableTime) {
                totalStableTimeSum += pair.second.dbl();
            }
            for (const auto& pair : lastMembershipChangeTime) {
                if (previousClusterHeads.find(pair.first) != previousClusterHeads.end()) {
                    simtime_t currentStable = simTime() - pair.second;
                    totalStableTimeSum += currentStable.dbl();
                }
            }
            avgStableTime = totalStableTimeSum / totalStabilityChanges;
        } else if (nodeCount > 0 && nodesWithStableMembership > 0) {
            avgStableTime = simTime().dbl();
        }
        
        double chLifetimeMean = 0.0;
        if (!clusterHeadLifetimes.empty()) {
            double sum = 0.0;
            for (double lifetime : clusterHeadLifetimes) {
                sum += lifetime;
            }
            chLifetimeMean = sum / clusterHeadLifetimes.size();
        }
        emit(globalClusterCountSignal, chCounter);
        emit(globalClusterHeadFairnessSignal, avgFairness);
        
        emit(globalClusterSizeMeanSignal, clusterSizeMean);
        emit(globalClusterStabilityAvgTimeSignal, avgStableTime);
        emit(globalClusterHeadLifetimeMeanSignal, chLifetimeMean);
        
        emit(globalControlOverheadSignal, avgControlOverhead);
        emit(globalReclusteringFrequencySignal, avgReclusteringFrequency);
        
        emit(globalThroughputSignal, avgThroughput);
        emit(globalEndToEndDelaySignal, avgDelay);
        emit(globalPacketDeliveryRatioSignal, avgPDR);
        
        emit(globalEnergyConsumptionVarianceSignal, energyVariance);
        emit(globalEnergyConsumptionSignal, avgEnergy);
        emit(globalLoadBalanceSignal, loadBalance);
        emit(globalNetworkLifetimeSignal, networkLifetime);
        
        scheduleAt(simTime() + interval, timerMsg);
    }
}

void Observer::finish() {
    cancelAndDelete(timerMsg);
}