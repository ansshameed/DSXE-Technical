#ifndef TRADER_SHVR_HPP
#define TRADER_SHVR_HPP

#include "traderagent.hpp"
#include <thread>
#include <mutex>
#include <random>
#include <chrono>
#include <vector>
#include "../utilities/syncqueue.hpp"
#include "../message/profitmessage.hpp"

#include <iostream> // to print full profitability
#include <iomanip>

class TraderShaver : public TraderAgent
{
public:

    TraderShaver(NetworkEntity *network_entity, TraderConfigPtr config)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit}
    {
        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);
    }

    void terminate() override
    {
        if (trading_thread_ != nullptr)
        {
            trading_thread_->join();
            delete(trading_thread_);
        }
        TraderAgent::terminate();
    }

    void onTradingStart() override
    {
        std::cout << "Trading window started.\n";
        is_trading_ = true;
    }

    void onTradingEnd() override
    {   
        is_trading_ = false;
        displayProfitability();
        //sendProfitToExchange();
        std::cout << "Trading window ended.\n";

    }

    void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) override
    {   
        std::unique_lock<std::mutex> lock(mutex_);
        if (!is_trading_) 
        { 
            return; 
        }
        lock.unlock(); 

        std::cout << "Received market data from " << exchange << "\n";
        //int quantity = 100;
 
        int quantity = getRandomOrderSize(); // Use random order size 
        double price = getShaverPrice(msg);
        placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
        std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << "\n";
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        // Order added to order book
        if (msg->order->status == Order::Status::NEW)
        {
            last_accepted_order_id_ = msg->order->id;
        }

        //std::cout << "Received execution report from " << exchange << ": Order: " << msg->order->id << " Status: " << msg->order->status << 
        //" Qty remaining = " << msg->order->remaining_quantity << "\n";

        //Calculate Profitability
        if(msg->order->status == Order::Status::FILLED || msg->order->status == Order::Status::PARTIALLY_FILLED) 
        {   
            if (msg->trade) { 
                Trade trade = {msg->trade->price, msg->trade->quantity, msg->order->side};
                executed_trades_.push_back(trade);
            }
        }
    }

    void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) override
    {
        throw std::runtime_error("Shaver trader does not cancel orders therefore cannot receive cancel rejection.");
    }

    void displayProfitability() 
    { 

        double buyer_profit = 0.0; 
        double seller_profit = 0.0; 

        // Calculate profit or loss
        for (const auto& trade : executed_trades_) 
        { 
            if (trade.side == Order::Side::ASK) // Sell order 
            { 
                seller_profit += trade.price * trade.quantity; 
            }
            else if (trade.side == Order::Side::BID) // Buy order 
            { 
                buyer_profit -= trade.price * trade.quantity; 
            }
        }

        total_profit_ = buyer_profit + seller_profit;
        sendProfitToExchange();
    }

private:

    void sendProfitToExchange()
        {
            std::cout << "[DEBUG] SHVR Trader Profit: " << std::fixed << std::setprecision(0) << total_profit_ << "\n";
        }

    double getShaverPrice(MarketDataMessagePtr msg)
    {
        if (trader_side_ == Order::Side::BID)
        {
            double shaved_price = msg->data->best_bid + 1;
            return std::min(shaved_price, limit_price_);
        }
        else
        {
            double shaved_price = msg->data->best_ask - 1;
            return std::max(shaved_price, limit_price_);
        }
    }

    void sleep()
    {
        std::uniform_real_distribution<> dist(-REL_JITTER, REL_JITTER);
        unsigned long jitter = dist(random_generator_);
        unsigned long sleep_time_ms = std::round(trade_interval_ms_ * (1.0 + jitter));
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
    }

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    unsigned int trade_interval_ms_ ; 

    bool is_trading_ = false;

    constexpr static double MIN_PRICE = 1.0;
    constexpr static double MAX_PRICE = 200.0;
    constexpr static double REL_JITTER = 0.25;

    std::optional<int> last_accepted_order_id_ = std::nullopt;

    // Profitability parameters 
    struct Trade { 
        double price;
        int quantity;
        Order::Side side;
    }; 
    std::vector<Trade> executed_trades_; 
    double total_profit_ = 0.0;
    std::string agent_name_ = "Shaver";

    // Mutex and Actively Trade attributes mechanism
    std::mt19937 random_generator_;
    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;

};

#endif