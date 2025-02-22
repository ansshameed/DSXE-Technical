#ifndef TRADER_ZIC_HPP
#define TRADER_ZIC_HPP

#include <random>

#include "traderagent.hpp"
#include "../message/profitmessage.hpp"

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

    std::string getAgentName() const override { return "ZIC"; }

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
        sendProfitToExchange();
        std::cout << "Trading window ended.\n";
        is_trading_ = false;
        lock.unlock();
    }

    void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) override
    {
        std::cout << "Received market data from " << exchange << "\n";
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        // Order added to order book
        if (msg->order->status == Order::Status::NEW)
        {
            last_accepted_order_id_ = msg->order->id;
        }

        if (msg->trade) { 
            // Debugging: Print trade details
            std::cout << "Trade Executed! Price: " << msg->trade->price
                      << " | Quantity: " << msg->trade->quantity
                      << " | Order ID: " << msg->order->id << std::endl;
    
            // Cast order to LimitOrder
            LimitOrderPtr limit_order = std::dynamic_pointer_cast<LimitOrder>(msg->order);
            if (!limit_order) {
                std::cerr << "Error: Failed to cast order to LimitOrder! Check order type." << std::endl;
                return;
            }
    
            // Debugging: Verify limit order details
            std::cout << "Limit Order Details - Price: " << limit_order->price
                      << " | Side: " << (limit_order->side == Order::Side::BID ? "BID" : "ASK") << std::endl;
    
            // Sanity check: Ensure the trade is within expected price limits
            if ((limit_order->side == Order::Side::BID && msg->trade->price > limit_order->price) ||
                (limit_order->side == Order::Side::ASK && msg->trade->price < limit_order->price)) {
                std::cerr << "Warning: Trade executed at unexpected price! Limit Order Price: " 
                          << limit_order->price << " | Trade Price: " << msg->trade->price << std::endl;
            }
    
            // Call bookkeeping function
            bookkeepTrade(msg->trade, limit_order);
            std::cout << "Bookkeeping complete for Order ID: " << limit_order->id << std::endl;
        }

        // std::cout << "Received execution report from " << exchange << ": Order: " << msg->order->id << " Status: " << msg->order->status << 
        // " Qty remaining = " << msg->order->remaining_quantity << "\n";
    }

    void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) override
    {
        std::cout << "Received cancel reject from " << exchange << ": Order: " << msg->order_id;
    }

private:

    void sendProfitToExchange()
    {
        ProfitMessagePtr profit_msg = std::make_shared<ProfitMessage>();
        profit_msg->agent_name = getAgentName(); 
        profit_msg->profit = balance; 
        sendMessageTo(exchange_, std::dynamic_pointer_cast<Message>(profit_msg), true);
    }   

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

        int quantity = 100;
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
};

#endif