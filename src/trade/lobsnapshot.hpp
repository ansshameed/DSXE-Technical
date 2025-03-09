#ifndef LOB_SNAPSHOT_HPP
#define LOB_SNAPSHOT_HPP

#include <string>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/string.hpp>

#include "../utilities/csvprintable.hpp"
#include "../order/order.hpp"  // Used for SIDE

/** The LOB Snapshot Data **/
class LOBSnapshot : public CSVPrintable, std::enable_shared_from_this<LOBSnapshot> {
    public:
        LOBSnapshot() = default;

        LOBSnapshot(std::string ticker, int side, unsigned long long timestamp, unsigned long long time_diff, double best_bid, double best_ask, double micro_price, double mid_price, double imbalance, double spread, double total_volume, double p_equilibrium, double smiths_alpha, double limit_price_chosen, double trade_price)
            : ticker(ticker), side(side), timestamp(timestamp), time_diff(time_diff), best_bid(best_bid), best_ask(best_ask), micro_price(micro_price), mid_price(mid_price), imbalance(imbalance), spread(spread), total_volume(total_volume), p_equilibrium(p_equilibrium), smiths_alpha(smiths_alpha), trade_price(trade_price), limit_price_chosen(limit_price_chosen) {}

        std::string ticker;
        double side; // 1 for BID, 0 for ASK
        unsigned long long timestamp;
        unsigned long long time_diff;
        double best_bid;
        double best_ask;
        double micro_price;
        double mid_price;
        double imbalance; 
        double spread;
        double total_volume; 
        double p_equilibrium; 
        double smiths_alpha; 
        double limit_price_chosen;
        double trade_price; 


        std::string describeCSVHeaders() const override
        {
            return "timestamp, time_diff, side, best_bid, best_ask, micro_price, mid_price, imbalance, spread, total_volume, p_equilibrium, smiths_alpha, limit_price_chosen, trade_price"; // CSV headers for the LOB Snapshot
        }

        std::string toCSV() const override
        {
            return std::to_string(timestamp) + ", " + std::to_string(time_diff) + ", " + std::to_string(side) + ", " + std::to_string(best_bid) + ", " + std::to_string(best_ask) + ", " + std::to_string(micro_price) + ", " + std::to_string(mid_price) + ", " + std::to_string(imbalance) + ", " + std::to_string(spread) + ", " + std::to_string(total_volume) + "," + std::to_string(p_equilibrium) + "," + std::to_string(smiths_alpha) + "," + std::to_string(limit_price_chosen) + "," + std::to_string(trade_price); // CSV data for the LOB Snapshot
        }

    private:

        friend std::ostream& operator<<(std::ostream& os, const LOBSnapshot& data)
        {
            os << "LOB Snapshot:\n" 
            << "SIDE: " << data.side << "\n"
            << "TIMESTAMP: " << data.timestamp << "\n"
            << "TIME DIFF: " << data.time_diff << "\n"
            << "BEST BID: $" << data.best_bid << "\n" 
            << "BEST ASK: $" << data.best_ask << "\n" 
            << "MICRO PRICE: $" << data.micro_price << "\n"
            << "MID PRICE: $" << data.mid_price << "\n"
            << "IMBALANCE: " << data.imbalance << "\n"
            << "SPREAD: " << data.spread << "\n"
            << "TOTAL VOLUME: " << data.total_volume << "\n";            //ADD P EQUILIBRIUM << AND SMITH'S ALPHA << HERE
            return os;
        }

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & timestamp;
            ar & time_diff;
            ar & side; 
            ar & best_bid;
            ar & best_ask;
            ar & micro_price;
            ar & mid_price;
            ar & imbalance;
            ar & spread;
            ar & total_volume;
            ar & p_equilibrium;
            ar & smiths_alpha; 
            ar & limit_price_chosen;
            ar & trade_price; 
        }
};

typedef std::shared_ptr<LOBSnapshot> LOBSnapshotPtr;

#endif