//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//


simple Observer {
    parameters:
        double interval @unit(s) = default(10s);
        double startTime @unit(s) = default(30s);
        @display("i=block/meter");
        
        // Global metrics
        @signal[globalClusterStability](type="long");
        
        // Cluster quality metrics
        @signal[globalClusterCount](type="long");
        @signal[globalClusterSizeMean](type="double");
        @signal[globalClusterStabilityAvgTime](type="double");
        @signal[globalClusterHeadLifetimeMean](type="double");
        
        // Overhead metrics
        @signal[globalControlOverhead](type="double");
        @signal[globalReclusteringFrequency](type="double");
        //@signal[globalComputationCost](type="double"); Each nodes does that itself
        
        // Communication efficiency metrics
        @signal[globalThroughput](type="double");
        @signal[globalEndToEndDelay](type="double");
        @signal[globalPacketDeliveryRatio](type="double");
        
        // Energy and resource metrics
        @signal[globalEnergyConsumptionVariance](type="double");
        @signal[globalEnergyConsumption](type="double");
        @signal[globalLoadBalance](type="double");
        @signal[globalNetworkLifetime](type="double");

        // Fairness metrics
        @signal[globalClusterHeadFairness](type="double");
        
        @statistic[clusterStability](title="Cluster Stability (Changes)"; source="globalClusterStability"; record=count,sum,vector);
        
        @statistic[clusterCount](title="Number of Clusters"; source="globalClusterCount"; record=last,mean,vector);
        @statistic[clusterSizeMean](title="Cluster Size Mean"; source="globalClusterSizeMean"; record=mean,vector);
        @statistic[clusterStabilityAvgTime](title="Cluster Stability (Avg Time Between Changes)"; source="globalClusterStabilityAvgTime"; record=mean,vector);
        @statistic[clusterHeadLifetimeMean](title="Cluster Head Lifetime Mean"; source="globalClusterHeadLifetimeMean"; record=mean,vector);
        
        @statistic[controlOverhead](title="Control Overhead"; source="globalControlOverhead"; record=mean,vector);
        @statistic[reclusteringFrequency](title="Re-clustering Frequency"; source="globalReclusteringFrequency"; record=mean,vector);
        //@statistic[computationCost](title="Computation Cost"; source="globalComputationCost"; record=mean,sum,vector);
        
        @statistic[throughput](title="Throughput"; source="globalThroughput"; record=mean,vector);
        @statistic[endToEndDelay](title="End-to-End Delay"; source="globalEndToEndDelay"; record=mean,vector);
        @statistic[packetDeliveryRatio](title="Packet Delivery Ratio"; source="globalPacketDeliveryRatio"; record=mean,vector);
        
        @statistic[energyConsumptionVariance](title="Energy Consumption (Variance)"; source="globalEnergyConsumptionVariance"; record=last,vector);
        @statistic[energyConsumption](title="Energy Consumption (Mean)"; source="globalEnergyConsumption"; record=last,mean,vector);
        @statistic[loadBalance](title="Load Balance"; source="globalLoadBalance"; record=mean,vector);
        @statistic[networkLifetime](title="Network Lifetime (FND)"; source="globalNetworkLifetime"; record=last,vector);

        @statistic[clusterHeadFairness](title="Cluster Head Fairness"; source="globalClusterHeadFairness"; record=mean,vector);
}