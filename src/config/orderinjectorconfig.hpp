#ifndef ORDER_INJECTOR_CONFIG_HPP
#define ORDER_INJECTOR_CONFIG_HPP

#include "agentconfig.hpp"
#include "../order/order.hpp"
#include <boost/serialization/base_object.hpp>

class OrderInjectorConfig : public AgentConfig {
public:

    OrderInjectorConfig() = default;

    // Exchange connection attributes
    std::string exchange_name;
    std::string exchange_addr;
    std::string ticker;

    // Supply/demand range attributes
    int supply_min_low; 
    int supply_min_high; 
    int supply_max_low; 
    int supply_max_high; 
    int demand_min_low; 
    int demand_min_high; 
    int demand_max_low; 
    int demand_max_high; 

private:
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version) {
        ar & boost::serialization::base_object<AgentConfig>(*this);
        ar & exchange_name;
        ar & exchange_addr;
        ar & ticker;
        ar & supply_min_low;
        ar & supply_min_high;
        ar & supply_max_low;
        ar & supply_max_high;
        ar & demand_min_low;
        ar & demand_min_high;
        ar & demand_max_low;
        ar & demand_max_high;
    }
};

typedef std::shared_ptr<OrderInjectorConfig> OrderInjectorConfigPtr;

#endif
