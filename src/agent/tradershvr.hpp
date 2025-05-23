#ifndef TRADER_SHVR_HPP
#define TRADER_SHVR_HPP

#include "traderagent.hpp"
#include "../message/profitmessage.hpp"
#include "../message/customer_order_message.hpp"

class TraderShaver : public TraderAgent
{
public:

    TraderShaver(NetworkEntity *network_entity, TraderConfigPtr config)
    : TraderAgent(network_entity, config),
        exchange_{config->exchange_name},
        ticker_{config->ticker},
        trader_side_{config->side},
        limit_price_{config->limit},
        trade_interval_ms_{config->trade_interval}, // Add this line
        cancelling_{config->cancelling},            // Add this line
        random_generator_{std::random_device{}()},
        mutex_{}
    {   
        
        // Mark as legacy agent
        is_legacy_trader_ = true;

        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);
    }

    std::string getAgentName() const override { return "shvr"; }

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
        //sendProfitToExchange();
        std::cout << "Trading window ended.\n";
        lock.unlock();
        is_trading_ = false;
    }

    void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) override
    {   
        std::cout << "Received market data from " << exchange << "\n";
        // Store the latest market data for use in activelyTrade
        last_market_data_ = msg; 
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {   

        if (msg->order->status == Order::Status::NEW)
        {
            last_accepted_order_id_ = msg->order->id;
        }

        if (msg->trade) { 
            // Cast to LimitOrder if needed
            std::cout << "Trade Executed! Price: " << msg->trade->price << " | Quantity: " << msg->trade->quantity << " | Order ID: " << msg->order->id << std::endl;
            LimitOrderPtr limit_order = std::dynamic_pointer_cast<LimitOrder>(msg->order);
            if (!limit_order) {
                throw std::runtime_error("Failed to cast order to LimitOrder.");
            }
            bookkeepTrade(msg->trade, limit_order);
        }

        std::cout << "Received execution report from " << exchange << ": Order: " << msg->order->id << " Status: " << msg->order->status << 
        " Qty remaining = " << msg->order->remaining_quantity << "\n";
    }

    void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) override
    {
        throw std::runtime_error("Shaver trader does not cancel orders therefore cannot receive cancel rejection.");
    }

    void handleBroadcastFrom(std::string_view sender, MessagePtr message) override
    {
        if (message->type == MessageType::CUSTOMER_ORDER) 
        {
            auto cust_msg = std::dynamic_pointer_cast<CustomerOrderMessage>(message);
            if (cust_msg) 
            { 
                std::lock_guard<std::mutex> lock(mutex_);
                customer_orders_.push(cust_msg); // Enqueue customer order
                std::cout << "[SHVR] Enqueued CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
            }
            return; // Just exit the function
        }
        // If it's not a customer order, call the base class handler
        TraderAgent::handleBroadcastFrom(sender, message);
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

    void placeOrder()
    {
        // If no market data yet but we have customer orders, use one to bootstrap
        if (!last_market_data_.has_value() && !customer_orders_.empty())
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_customer_order_ = customer_orders_.top();
            customer_orders_.pop();
            limit_price_ = current_customer_order_.value()->price;
            trader_side_ = current_customer_order_.value()->side;
            
            // Process customer order directly to bootstrap market activity
            std::uniform_int_distribution<int> dist(10, 50);
            int quantity = dist(random_generator_);
            double price = current_customer_order_.value()->price;
            
            placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
            std::cout << ">> Customer Order (Bootstrap): " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << "\n";
            return;
        }

        // For normal operation, require market data
        if (!last_market_data_.has_value()) {
            std::cout << "No market data available yet, skipping order placement.\n";
            return;
        }

        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, trader_side_, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }

        // Process customer orders with market data if available
        if (!customer_orders_.empty())
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_customer_order_ = customer_orders_.top();
            customer_orders_.pop();
            limit_price_ = current_customer_order_.value()->price;
            trader_side_ = current_customer_order_.value()->side;
        }

        std::uniform_int_distribution<int> dist(10, 50);
        int quantity = dist(random_generator_);
        double price = getShaverPrice(last_market_data_.value(), limit_price_);
        placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
        std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << "\n";
    }

    double getShaverPrice(MarketDataMessagePtr msg, double limit_price_)
    {
        double price;

        if (trader_side_ == Order::Side::BID)
        {
            price = std::min(msg->data->best_bid + 1, limit_price_);
            std::cout << "Using Shaver Price" << "\n"; 
        }
        else
        {
            price = std::max(msg->data->best_ask - 1, limit_price_);
            std::cout << "Using Shaver Price" << "\n"; 
        }

        return price;
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


    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    unsigned int trade_interval_ms_; 

    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;
    std::optional<int> last_accepted_order_id_ = std::nullopt;
    
    std::mt19937 random_generator_;
    
    constexpr static double MIN_PRICE = 1.0;
    constexpr static double MAX_PRICE = 200.0;
    constexpr static double REL_JITTER = 0.25;

    std::optional<CustomerOrderMessagePtr> current_customer_order_;
    std::stack<CustomerOrderMessagePtr> customer_orders_;
    std::optional<MarketDataMessagePtr> last_market_data_;

    bool cancelling_; 
};

#endif