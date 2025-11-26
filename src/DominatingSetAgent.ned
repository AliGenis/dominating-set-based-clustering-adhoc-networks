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
//
// DominatingSetAgent.ned
//

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
//
// DominatingSetAgent.ned
//

simple DominatingSetAgent like inet.applications.contract.IApp
{
    parameters:
        @class(DominatingSetAgent);
        @display("i=block/app");

        double helloInterval @unit(s) = default(5s);
        double algorithmInterval @unit(s) = default(5s);
        double neighborPurgeTimeout @unit(s) = default(10s);

        int localPort = default(5000);
        int destPort = default(5000);

    gates:
        input socketIn @labels(UdpControlInfo/up);
        output socketOut @labels(UdpControlInfo/down);
}









































//package ds_clustering;

//import inet.applications.contract.IApp;
//
//simple DominatingSetAgent like IApp
//{
//    parameters:
//        @class(DominatingSetAgent);
//        @display("i=block/app");
//
//        // Timing parameters
//        double helloInterval @unit(s) = default(1s);
//        double algorithmInterval @unit(s) = default(5s);
//        double neighborPurgeTimeout @unit(s) = default(10s);
//
//        // Port configuration
//        int localPort = default(5000);
//        int destPort = default(5000);
//
//        // Statistics
//        @signal[controlOverhead](type=long);
//        @signal[clusterHeadChange](type=long);
//        @signal[amIClusterHead](type=double);
//
//        @statistic[controlOverhead](title="Control Overhead"; record=count,sum,vector);
//        @statistic[clusterHeadChanges](title="Cluster Head Changes"; source=clusterHeadChange; record=count,sum,vector);
//        @statistic[isClusterHead](title="Is Cluster Head"; source=amIClusterHead; record=timeavg,vector);
//
//    gates:
//        input socketIn @labels(UdpControlInfo/up);
//        output socketOut @labels(UdpControlInfo/down);
//}