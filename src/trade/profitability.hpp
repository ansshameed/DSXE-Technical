#ifndef PROFITABILITY_RECORD_HPP
#define PROFITABILITY_RECORD_HPP

#include <string>
#include <memory>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/string.hpp>

#include "../utilities/csvprintable.hpp"

/** Profitability Record Class **/
class ProfitabilityRecord : public CSVPrintable, public std::enable_shared_from_this<ProfitabilityRecord> {
public:
    ProfitabilityRecord() = default;

    ProfitabilityRecord(std::string trader_type, std::string side, double buyer_profit, double seller_profit, double total_profit)
        : trader_type_(trader_type), side_(side), buyer_profit_(buyer_profit), seller_profit_(seller_profit), total_profit_(total_profit) {}

    std::string describeCSVHeaders() const override
    {
        return "Trader Type, Side, Buyer Profit, Seller Profit, Total Profit";
    }

    std::string toCSV() const override
    {
        return trader_type_ + ", " + side_ + ", " + std::to_string(buyer_profit_) + ", " + std::to_string(seller_profit_) + ", " + std::to_string(total_profit_);
    }

private:
    std::string trader_type_;
    std::string side_;
    double buyer_profit_;
    double seller_profit_;
    double total_profit_;

    /** Enable serialization */
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & trader_type_;
        ar & side_;
        ar & buyer_profit_;
        ar & seller_profit_;
        ar & total_profit_;
    }

    /** Stream output operator for debugging */
    friend std::ostream& operator<<(std::ostream& os, const ProfitabilityRecord& record)
    {
        os << "Profitability Record:\n"
           << "Trader Type: " << record.trader_type_ << "\n"
           << "Side: " << record.side_ << "\n"
           << "Buyer Profit: $" << record.buyer_profit_ << "\n"
           << "Seller Profit: $" << record.seller_profit_ << "\n"
           << "Total Profit: $" << record.total_profit_ << "\n";
        return os;
    }
};

typedef std::shared_ptr<ProfitabilityRecord> ProfitabilityRecordPtr;

#endif
