#ifndef TRADER_CONFIG_HPP
#define TRADER_CONFIG_HPP

#include "agentconfig.hpp"
#include "../order/order.hpp"
#include <boost/serialization/base_object.hpp>

class TraderConfig : public AgentConfig
{
public:

    TraderConfig() = default;

    std::string name; // agent name
    std::string exchange_name;
    std::string exchange_addr;
    std::string ticker;
    Order::Side side;
    double limit;
    unsigned int delay;
    unsigned int trade_interval;
    bool cancelling;

private:
    
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & boost::serialization::base_object<AgentConfig>(*this);
        ar & name;
        ar & exchange_name;
        ar & exchange_addr;
        ar & ticker;
        ar & side;
        ar & limit;
        ar & delay;
        ar & trade_interval;
        ar & cancelling;
    }

};

typedef std::shared_ptr<TraderConfig> TraderConfigPtr;

#endif