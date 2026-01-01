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

        @signal[computationCost](type=double);
        @signal[energyConsumption](type=double);
        @signal[endToEndDelay](type=double);

        @statistic[computationCost](title="Computation Cost"; source=computationCost; record=mean,sum,vector);
        @statistic[energyConsumption](title="Energy Consumption (Per Node)"; source=energyConsumption; record=last,vector);
        @statistic[endToEndDelay](title="End To End Delay (Per Packet in ms)"; source=endToEndDelay; record=mean, vector);

    gates:
        input socketIn @labels(UdpControlInfo/up);
        output socketOut @labels(UdpControlInfo/down);
}