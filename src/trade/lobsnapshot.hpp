#ifndef LOB_SNAPSHOT_HPP
#define LOB_SNAPSHOT_HPP

#include <string>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/string.hpp>

#include "../utilities/csvprintable.hpp"

/** The LOB Snapshot Data **/
class LOBSnapshot : public CSVPrintable, std::enable_shared_from_this<LOBSnapshot> {
    public:
        LOBSnapshot() = default;

        LOBSnapshot(std::string ticker, unsigned long long timestamp, double best_bid, double best_ask, double micro_price, double mid_price)
            : ticker(ticker), timestamp(timestamp), best_bid(best_bid), best_ask(best_ask), micro_price(micro_price), mid_price(mid_price) {}

        std::string ticker;
        unsigned long long timestamp;
        double best_bid;
        double best_ask;
        double micro_price;
        double mid_price;


        std::string describeCSVHeaders() const override
        {
            return "timestamp,best_bid,best_ask,micro_price,mid_price"; // CSV headers for the LOB Snapshot
        }

        std::string toCSV() const override
        {
            return std::to_string(timestamp) + "," + std::to_string(best_bid) + "," + std::to_string(best_ask) + "," + std::to_string(micro_price) + "," + std::to_string(mid_price);
        }

    private:
        friend std::ostream& operator<<(std::ostream& os, const LOBSnapshot& data)
        {
            os << "LOB Snapshot:\n" 
            << "TIMESTAMP: " << data.timestamp << "\n"
            << "BEST BID: $" << data.best_bid << "\n" 
            << "BEST ASK: $" << data.best_ask << "\n" 
            << "MICRO PRICE: $" << data.micro_price << "\n"
            << "MID PRICE: $" << data.mid_price << "\n";
            return os;
        }

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & timestamp;
            ar & best_bid;
            ar & best_ask;
            ar & micro_price;
            ar & mid_price;
        }
};

typedef std::shared_ptr<LOBSnapshot> LOBSnapshotPtr;

#endif