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
// MyNetwork.ned
//

import inet.node.inet.ManetRouter;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.physicallayer.wireless.ieee80211.packetlevel.Ieee80211ScalarRadioMedium;
import inet.visualizer.canvas.integrated.IntegratedCanvasVisualizer;
import inet.environment.common.PhysicalEnvironment;

import DominatingSetAgent;
import Observer;

network MyManet
{
    parameters:
        int numHosts = default(20);
        double areaWidth @unit(m) = default(500m);
        double areaHeight @unit(m) = default(500m);
        @display("bgb=600,600");

    submodules:
        configurator: Ipv4NetworkConfigurator {
            @display("p=50,50");
        }

        radioMedium: Ieee80211ScalarRadioMedium {
            @display("p=50,100");
        }

        physicalEnvironment: PhysicalEnvironment {
            @display("p=50,150");
        }

        visualizer: IntegratedCanvasVisualizer {
            @display("p=50,200");
        }

        host[numHosts]: ManetRouter {
        }

        observer: Observer {
            interval = 10s;
            @display("p=50,250");
        }
}