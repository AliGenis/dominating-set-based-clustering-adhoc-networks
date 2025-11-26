#include "Aodv.h"
#include "../DominatingSetAgent.h"

using namespace inet::aodv;

class MyAodv : public Aodv {
    protected:
        DominatingSetAgent *dominatingSetAgent = nullptr;
        
        virtual void initialize(int stage) override;
        
        virtual void handleRREQ(const Ptr<Rreq>& rreq, const L3Address& sourceAddr, unsigned int timeToLive);
};
