#ifndef TRADE_HPP
#define TRADE_HPP

#include <string>
#include <chrono>

#include <boost/serialization/serialization.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/shared_ptr.hpp>

#include "../utilities/csvprintable.hpp"

class TradeFactory;

class Trade : std::enable_shared_from_this<Trade>, public CSVPrintable
{
public: 

    Trade()
    {
        std::chrono::system_clock::duration now = std::chrono::system_clock::now().time_since_epoch();
        timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    }

    int id;
    std::string ticker;
    int quantity;
    double price;
    unsigned long long timestamp = 0;
    int buyer_id;
    int seller_id;
    std::string buyer_name;
    std::string seller_name;
    int aggressing_order_id;
    int resting_order_id;
    double buyer_priv_value;
    double seller_priv_value;
    double buyer_profit; 
    double seller_profit; 


    std::string describeCSVHeaders() const override
    {
        return "id,ticker,quantity,price,timestamp,buyer_id,seller_id,buyer_name,seller_name,aggressing_order_id,resting_order_id,buyer_priv_value,seller_priv_value,buyer_profit,seller_profit"; 
    }

    std::string toCSV() const override
    {
        return std::to_string(id) + "," + ticker + "," + std::to_string(quantity) + "," + std::to_string(price) + "," + std::to_string(timestamp) + "," + std::to_string(buyer_id) + "," + std::to_string(seller_id) + "," + buyer_name + "," + seller_name + "," + std::to_string(aggressing_order_id) + "," + std::to_string(resting_order_id) + "," + std::to_string(buyer_priv_value) + "," + std::to_string(seller_priv_value) + "," + std::to_string(buyer_profit) + "," + std::to_string(seller_profit);
    }

private:

    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & boost::serialization::base_object<CSVPrintable>(*this);
        ar & id;
        ar & ticker;
        ar & quantity;
        ar & price;
        ar & timestamp;
        ar & buyer_id;
        ar & seller_id;
        ar & buyer_name;
        ar & seller_name;
        ar & aggressing_order_id;
        ar & resting_order_id;
        ar & buyer_priv_value;
        ar & seller_priv_value;
        ar & buyer_profit;
        ar & seller_profit;
    }

    friend std::ostream& operator<<(std::ostream& os, const Trade& trade)
    {
        os << trade.timestamp << " [Trade] Id: " << trade.id << " " << trade.ticker << " " << trade.quantity << " @ $" << trade.price << " Buyer: " << trade.buyer_id << " Seller: " << trade.seller_id << " Buyer Name: " << trade.buyer_name << " Seller Name: " << trade.seller_name;
        return os;
    }
};

typedef std::shared_ptr<Trade> TradePtr;

#endif