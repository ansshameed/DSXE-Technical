#ifndef TRADER_OBI_HPP
#define TRADER_OBI_HPP

#include <vector>
#include <numeric>
#include <algorithm>
#include "traderagent.hpp"
#include "order/orderbook.hpp"
#include "order/limitorder.hpp"
#include "trade/trade.hpp"

class TraderOBI : public TraderAgent
{
public:

    TraderOBI(NetworkEntity *network_entity, TraderConfigPtr config)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      cancelling_{config->cancelling},
      trade_interval_ms_{config->trade_interval}, 
      random_generator_{std::random_device{}()},
      profit_margin_{0.0}, // Initialise profit margin
      mutex_{}
    {
        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);

        // Initialize the order book
        order_book_ = OrderBook::create(ticker_);
    }

    void onTradingStart() override
    {
        std::cout << "Trading window started.\n";
        is_trading_ = true;
        activelyTrade();
    }

    void onTradingEnd() override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        std::cout << "Trading window ended.\n";
        is_trading_ = false;
        lock.unlock();
    }

    void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) override
    {
        std::cout << "Received market data from " << exchange << "\n";
        std::cout << "Last price traded: " << msg->data->last_price_traded << "\n";

        // Update the order book with the latest market data
        updateOrderBook(msg);

        // Calculate Order Book Imbalance (OBI)
        double obi = calculateOBI();
        std::cout << "Order Book Imbalance (OBI): " << obi << "\n";

        // Implement trading logic based on OBI
        if (trader_side_ == Order::Side::BID && obi > 0.5) { // Strong buy pressure
            placeOrder(Order::Side::BID);
        } 
        else if (trader_side_ == Order::Side::ASK && obi < -0.5) { // Strong sell pressure
            placeOrder(Order::Side::ASK);
        }
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        // Order added to order book
        if (msg->order->status == Order::Status::NEW) //Checks if order is new (NEW = order successfully added to LOB by exchange)
        {
            last_accepted_order_id_ = msg->order->id; //Tracks most recent order successfully added to LOB (can be used later for cancel/modifying)
        }
    }

    void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) override
    {
        std::cout << "Received cancel reject from " << exchange << ": Order: " << msg->order_id;
    }

private:

    void activelyTrade()
    {
        trading_thread_ = new std::thread([&, this](){

            std::unique_lock<std::mutex> lock(mutex_);
            while (is_trading_)
            {
                lock.unlock();

                sleep();
                lock.lock();
            }
            lock.unlock();

            std::cout << "Finished actively trading.\n";
        });
    }

    void sleep()
    {
        // Generate a random jitter
        std::uniform_real_distribution<> dist(-REL_JITTER, REL_JITTER);
        unsigned long jitter = dist(random_generator_);
        unsigned long sleep_time_ms = std::round(trade_interval_ms_ * (1.0+jitter));

        // Sleep for specified duration
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
    }

    double calculateOBI()
    {
        double total_bid_volume = order_book_->getTotalBidVolume();
        double total_ask_volume = order_book_->getTotalAskVolume();

        if (total_bid_volume + total_ask_volume == 0) {
            return 0.0; // Avoid division by zero
        }

        double imbalance = (total_bid_volume - total_ask_volume) / (total_bid_volume + total_ask_volume);
        return imbalance;
    }

    void placeOrder(Order::Side side) //Places limit order on exchange 
    {
        if (cancelling_ && last_accepted_order_id_.has_value()) //Check if cancelling is enabled and last order was accepted
        {
            cancelOrder(exchange_, side, ticker_, last_accepted_order_id_.value()); //Cancel last accepted order
            last_accepted_order_id_ = std::nullopt; //Clears last accepted order to null
        }

        int quantity = getRandomOrderSize(); // Use random order size
        double price = getQuotePrice(side); //Get quote price based on trading side (BID or ASK)    
        placeLimitOrder(exchange_, side, ticker_, quantity, price, limit_price_); //Place limit order on exchange

        std::cout << ">> " << (side == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << "\n"; //Logs the order
    }

    double getQuotePrice(Order::Side side) //Adjust for side
    {
        double price = round(limit_price_ * (1 + profit_margin_)); //Adjust limit price based on profit margin (positive margin = increase ASK order, negative margin = decrease BID order)
        if (side == Order::Side::BID) //If BID order, adjust price to be less than limit price
        {
            price = std::min(limit_price_, price);
        }
        else //If ASK order, adjust price to be greater than limit price 
        {
            price = std::max(limit_price_, price);
        }
        return price; //Return adjusted price for limit order
    }

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    bool cancelling_;
    unsigned int trade_interval_ms_;
    std::vector<double> closing_prices_;

    std::optional<int> last_accepted_order_id_ = std::nullopt;

    std::mt19937 random_generator_;

    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;

    double profit_margin_; 
    constexpr static double REL_JITTER = 0.25;

    OrderBookPtr order_book_; // Order book for the ticker
};

#endif