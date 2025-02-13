#ifndef TRADER_ZIC_HPP
#define TRADER_ZIC_HPP

#include <random>

#include "traderagent.hpp"
#include "../message/profitmessage.hpp"

#include <iostream> // to print full profitability
#include <iomanip>

/** Prototype ZIC trader implementation. */
class TraderZIC : public TraderAgent
{
public:

    TraderZIC(NetworkEntity *network_entity, TraderConfigPtr config)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      cancelling_{config->cancelling},
      trade_interval_ms_{config->trade_interval},
      random_generator_{std::random_device{}()},
      mutex_{}
    {
        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);
    }

    /** Gracefully terminates the trader, freeing all memory. */
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
        activelyTrade();
    }

    void onTradingEnd() override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        is_trading_ = false;
        std::cout << "Trading window ended.\n";
        displayProfitability();
        sendProfitToExchange(); 
        lock.unlock();
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
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        // Order added to order book
        if (msg->order->status == Order::Status::NEW)
        {
            last_accepted_order_id_ = msg->order->id;
        }

        // std::cout << "Received execution report from " << exchange << ": Order: " << msg->order->id << " Status: " << msg->order->status << 
        // " Qty remaining = " << msg->order->remaining_quantity << "\n";

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
        std::cout << "Received cancel reject from " << exchange << ": Order: " << msg->order_id;
    }

    void displayProfitability() 
    { 
        // Calculate profit or loss

        for (const auto& trade : executed_trades_) 
        { 
            if (trade.side == Order::Side::ASK) // Sell order 
            { 
                total_profit_ += trade.price * trade.quantity; 
            }
            else if (trade.side == Order::Side::BID) // Buy order 
            { 
                total_profit_ -= trade.price * trade.quantity; 
            }
        }

        std::cout << "Total Profit: " << std::fixed << std::setprecision(0) << total_profit_ << "\n";
        
    }

private:

    void activelyTrade()
    {
        trading_thread_ = new std::thread([&, this](){

            std::unique_lock<std::mutex> lock(mutex_);
            while (is_trading_)
            {
                lock.unlock();

                placeOrder();
                sleep();

                lock.lock();
            }
            lock.unlock();
            std::cout << "Finished actively trading.\n";
        });
    }

    void sendProfitToExchange()
        {
            ProfitMessagePtr profit_msg = std::make_shared<ProfitMessage>();
            profit_msg->agent_id = this->agent_id;
            profit_msg->agent_name = agent_name_;
            profit_msg->profit = total_profit_;
            sendMessageTo(exchange_, std::dynamic_pointer_cast<Message>(profit_msg), true);
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

    void placeOrder()
    {
        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, trader_side_, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }

        //int quantity = 100;
        int quantity = getRandomOrderSize();
        double price = getRandomPrice();
        placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);

        std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") 
        << " " << quantity << " @ " << price << "\n";
    }

    double getRandomPrice()
    {
        if (trader_side_ == Order::Side::BID)
        {
            std::uniform_int_distribution<> dist(MIN_PRICE, limit_price_);
            return dist(random_generator_);
        }
        else
        {
            std::uniform_int_distribution<> dist(limit_price_, MAX_PRICE);
            return dist(random_generator_);
        }
    }

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    unsigned int trade_interval_ms_;
    bool cancelling_;

    std::optional<int> last_accepted_order_id_ = std::nullopt;

    std::mt19937 random_generator_;

    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;

    constexpr static double MIN_PRICE = 1.0;
    constexpr static double MAX_PRICE = 200.0;
    constexpr static double REL_JITTER = 0.25;

    // Profitability parameters 
    struct Trade { 
        double price;
        int quantity;
        Order::Side side;
    }; 
    std::vector<Trade> executed_trades_; 
    double total_profit_ = 0.0;

    std::string agent_name_ = "ZIC";

};

#endif