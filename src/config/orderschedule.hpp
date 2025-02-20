#ifndef ORDER_SCHEDULE_HPP
#define ORDER_SCHEDULE_HPP

#include <vector>
#include <string>
#include <utility>
#include <memory>

#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/utility.hpp> 

class OrderSchedule 
{
public: 
    // Supply/demand range parameters:
    // Range of values between which the max possible sell order will be randomly placed.
    int supply_min_low = 0; 
    int supply_min_high = 100;  

    // Range of values between which the min possible sell order will be randomly placed.
    int supply_max_low = 100;
    int supply_max_high = 200;

    // Range of values between which the max possible buy order will be randomly placed.
    int demand_min_low = 0; 
    int demand_min_high = 100;

    // Range of values between which the min possible buy order will be randomly placed.
    int demand_max_low = 100;
    int demand_max_high = 200;

    // Offset events from CSV: (normalized_time, scaled_offset)
    std::vector<std::pair<double, int>> offset_events;

    // Default constructor 
    OrderSchedule() = default; 

    OrderSchedule(int supply_min_low, int supply_min_high,
                  int supply_max_low, int supply_max_high,
                  int demand_min_low, int demand_min_high,
                  int demand_max_low, int demand_max_high)
    : supply_min_low{supply_min_low},
      supply_min_high{supply_min_high},
      supply_max_low{supply_max_low},
      supply_max_high{supply_max_high},
      demand_min_low{demand_min_low},
      demand_min_high{demand_min_high},
      demand_max_low{demand_max_low},
      demand_max_high{demand_max_high}
    {
    }

private:
    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        // Read/write each member
        ar & supply_min_low;
        ar & supply_min_high;
        ar & supply_max_low;
        ar & supply_max_high;
        ar & demand_min_low;
        ar & demand_min_high;
        ar & demand_max_low;
        ar & demand_max_high;
        ar & offset_events;
    }
};

std::vector<std::pair<double, int>> getOffsetEventList(const std::string &historical_data_file);
int realWorldScheduleOffset(double t, double total_time, const std::vector<std::pair<double, int>> &offset_events);
int scheduleOffset(double time);

using OrderSchedulePtr = std::shared_ptr<OrderSchedule>;

#endif
