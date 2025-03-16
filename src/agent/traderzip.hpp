#ifndef TRADER_ZIP_HPP
#define TRADER_ZIP_HPP

#include <random>
#include <stack> 

#include "traderagent.hpp"
#include "../config/zipconfig.hpp"
#include "../message/profitmessage.hpp"
#include "../message/customer_order_message.hpp"

/** Real-time implementation of the ZIP trading algorithm. */
class TraderZIP : public TraderAgent
{
public:

    TraderZIP(NetworkEntity *network_entity, ZIPConfigPtr config)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      cancelling_{config->cancelling},
      min_margin_{config->min_margin},
      trade_interval_ms_{config->trade_interval},
      liquidity_interval_ms_{config->liquidity_interval},
      random_generator_{std::random_device{}()},
      mutex_{}
    {
        initialiseConstants();

        // Mark as legacy agent
        is_legacy_trader_ = true;

        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);
    }

    std::string getAgentName() const override { return "zip"; }

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

    void handleBroadcastFrom(std::string_view sender, MessagePtr message) override
    {
        if (message->type == MessageType::CUSTOMER_ORDER) 
        {
            auto cust_msg = std::dynamic_pointer_cast<CustomerOrderMessage>(message);
            if (cust_msg) 
            { 
                std::lock_guard<std::mutex> lock(mutex_);
                customer_orders_.push(cust_msg); // Enqueue customer order
                std::cout << "[ZIP] Enqueued CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
            }
            return;
        }
        TraderAgent::handleBroadcastFrom(sender, message);
    }


    void onTradingStart() override
    {
        std::cout << "Trading window started.\n";
        next_undercut_timestamp_ = timeNow() + (liquidity_interval_ms_ * MS_TO_NS);
        next_lower_margin_timestamp_ = timeNow() + (trade_interval_ms_ * MS_TO_NS);
        is_trading_ = true;
        activelyTrade();
    }

    void onTradingEnd() override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        //sendProfitToExchange();
        std::cout << "Trading window ended.\n";
        // Delay shutdown to allow profit message to be sent completely.
        lock.unlock();
        is_trading_ = false;
    }

    void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) override
    {   
        std::cout << "Received market data from " << exchange << "\n";
        reactToMarket(msg);
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        // Order added to order book
        if (msg->order->status == Order::Status::NEW)
        {
            last_accepted_order_id_ = msg->order->id;
        }
        else if (msg->order->status == Order::Status::FILLED)
        {
            last_accepted_order_id_ = std::nullopt;
            next_lower_margin_timestamp_ = timeNow() + (trade_interval_ms_ * MS_TO_NS);
        }

        if (msg->trade) { 
            // Cast to LimitOrder if needed
            LimitOrderPtr limit_order = std::dynamic_pointer_cast<LimitOrder>(msg->order);
            if (!limit_order) {
                throw std::runtime_error("Failed to cast order to LimitOrder.");
            }
            bookkeepTrade(msg->trade, limit_order);
        }
    }

    void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) override
    {

    }

private:

    void sendProfitToExchange()
    {
        ProfitMessagePtr profit_msg = std::make_shared<ProfitMessage>();
        profit_msg->agent_name = getAgentName(); 
        profit_msg->profit = balance; 
        sendMessageTo(exchange_, std::dynamic_pointer_cast<Message>(profit_msg), true);
    }

    void initialiseConstants()
    {
        momentum_ = getRandom(0.0, 0.1);
        learning_rate_ = getRandom(0.0, 0.5);

        if (trader_side_ == Order::Side::BID)
        {
            profit_margin_ = getRandom(0.05, 0.35) * -1.0;
        }
        else
        {
            profit_margin_ = getRandom(0.05, 0.35);
        }

        prev_change_ = 0.0;
        last_price_ = getQuotePrice();
        last_client_order_id_ = 0;
        last_accepted_order_id_ = std::nullopt;
        last_market_data_ = std::nullopt;

        std::cout << "mom=" << momentum_ << "\n";
        std::cout << "lr=" << learning_rate_ << "\n";
        std::cout << "margin=" << profit_margin_ << "\n";
    }

    void activelyTrade()
    {
        trading_thread_ = new std::thread([&, this](){

            std::unique_lock<std::mutex> lock(mutex_);
            while (is_trading_)
            {
                lock.unlock();

                // Undercut competition if market not liquid
                if (timeNow() >= next_undercut_timestamp_)
                {
                    undercutCompetition();
                }

                // Place order
                try {
                    placeOrder();
                } catch (const std::exception& e) {
                    std::cerr << "ERROR: ZIP Trader crashed with exception: " << e.what() << std::endl;
                }

                sleep();

                lock.lock();
            }
            lock.unlock();

            std::cout << "Finished actively trading.\n";
        });
    }

    void reactToMarket(MarketDataMessagePtr msg)
    {
        // If trade has occurred always increase margin towards trade price
        // Only decrease margin, if previous order has not executed within the trade interval

        // If trade has occurred

        if (!last_market_data_.has_value()) { 
            last_market_data_ = msg->data; // Ensure first valid data is stored
            return;
        }

        if (last_market_data_.has_value() && msg->data->cumulative_volume_traded > last_market_data_.value()->cumulative_volume_traded)
        {
            // And trade price went up
            if (msg->data->last_price_traded > last_market_data_.value()->last_price_traded)
            {
                // Raise margin for sellers
                if (trader_side_ == Order::Side::ASK && last_price_ <= msg->data->last_price_traded)
                {
                    double target = increaseTargetPrice(msg->data->last_price_traded);
                    updateMargin(target);
                }
            }
            // And trade price went down
            else if (msg->data->last_price_traded < last_market_data_.value()->last_price_traded)
            {
                // Raise margin for buyers
                if (trader_side_ == Order::Side::BID && last_price_ >= msg->data->last_price_traded)
                {
                    double target = decreaseTargetPrice(msg->data->last_price_traded);
                    updateMargin(target);
                }
            }

            // Lower margin for buyers (conditional)
            if (trader_side_ == Order::Side::BID && timeNow() > next_lower_margin_timestamp_)
            {
                double target = increaseTargetPrice(msg->data->last_price_traded);
                updateMargin(target);
                next_lower_margin_timestamp_ = timeNow() + (trade_interval_ms_ * MS_TO_NS);
            }
            // Lower margin for sellers (conditional)
            else if (trader_side_ == Order::Side::ASK && timeNow() > next_lower_margin_timestamp_)
            {
                double target = decreaseTargetPrice(msg->data->last_price_traded);
                updateMargin(target);
                next_lower_margin_timestamp_ = timeNow() + (trade_interval_ms_ * MS_TO_NS);
            }

            // Update last global trade info
            next_undercut_timestamp_ = timeNow() + (liquidity_interval_ms_ * MS_TO_NS);
        }
        else if (!last_market_data_.has_value())
        {
            // Update last global trade info
            next_undercut_timestamp_ = timeNow() + (liquidity_interval_ms_ * MS_TO_NS);
        }

        // Save this market data snapshot
        last_market_data_ = msg->data;

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

    void undercutCompetition()
    {   
        if (!last_market_data_.has_value()) {
            std::cout << "No market data available to undercut.\n";
            return;
        }

        // Undercut competition by adjusting the price towards best competing bid/offer
        if (trader_side_ == Order::Side::BID)
        {
            if (last_market_data_.value()->best_ask_size > 0)
            {
                double best_ask = last_market_data_.value()->best_ask;
                double target = increaseTargetPrice(best_ask);
                updateMargin(target);

                next_undercut_timestamp_ = timeNow() + (liquidity_interval_ms_ * MS_TO_NS);
                next_lower_margin_timestamp_ = timeNow() + (trade_interval_ms_ * MS_TO_NS);
            }
        }
        else
        {
            if (last_market_data_.value()->best_bid_size > 0)
            {
                double best_bid = last_market_data_.value()->best_bid;
                double target = decreaseTargetPrice(best_bid);
                updateMargin(target);

                next_undercut_timestamp_ = timeNow() + (liquidity_interval_ms_ * MS_TO_NS);
                next_lower_margin_timestamp_ = timeNow() + (trade_interval_ms_ * MS_TO_NS);
            }
        }
    }

    double getQuotePrice()
    {   
        if (!last_market_data_.has_value()) {
            std::cout << "No market data available, returning default limit price.\n";
            return limit_price_; 
        }

        double price = round(limit_price_ * (1 + profit_margin_));
        if (trader_side_ == Order::Side::BID)
        {
            price = std::min(limit_price_, price);
        }
        else
        {
            price = std::max(limit_price_, price);
        }
        return price;
    }

    void updateMargin(double target_price)
    {
        double diff = target_price - last_price_;
        double change = ((1.0 - momentum_) * (learning_rate_ * diff)) + (momentum_ * prev_change_);
        
        prev_change_ = change;
        double new_margin = ((last_price_ + change) / limit_price_) - 1.0;

        if (trader_side_ == Order::Side::BID)
        {
            profit_margin_ = std::min(-min_margin_, new_margin);
        }
        else if (trader_side_ == Order::Side::ASK)
        {
            profit_margin_ = std::max(min_margin_, new_margin);
        }

        std::cout << "[Margin Update] Target = " << target_price << " Margin = " << new_margin << " Actual = " << profit_margin_ << "\n";
    }

    double increaseTargetPrice(double price)
    {
        double abs_perturbation = C_A * getRandom(0.0, 1.0);
        double rel_perturbation = (1.0 + (C_R * getRandom(0.0, 1.0))) * price;
        double target = round(abs_perturbation + rel_perturbation);
        return target;
    }

    double decreaseTargetPrice(double price)
    {
        double abs_perturbation = C_A * getRandom(0.0, 1.0);
        double rel_perturbation = (1.0 - (C_R * getRandom(0.0, 1.0))) * price;
        double target = round(rel_perturbation - abs_perturbation);
        return target;
    }

    void placeOrder()
    {
        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, trader_side_, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!customer_orders_.empty()) 
        {   
            auto cust_order = customer_orders_.top(); // Get next customer order
            customer_orders_.pop();
            limit_price_ = cust_order->price;
            //trader_side_ = cust_order->side;
        }

        last_price_ = getQuotePrice();
        std::uniform_int_distribution<int> dist(10, 50);
        int quantity = dist(random_generator_);
        placeLimitOrder(exchange_, trader_side_, ticker_, quantity, last_price_, limit_price_, Order::TimeInForce::GTC, ++last_client_order_id_);
        std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << 100 << " @ " << last_price_ << "\n";
    }


    unsigned long long timeNow()
    {
        std::chrono::system_clock::duration now = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    }

    double getRandom(double lower, double upper)
    {
        std::uniform_real_distribution<> dist(lower, upper);
        return dist(random_generator_);
    }

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double learning_rate_;
    double momentum_;
    double limit_price_;
    double min_margin_;
    bool cancelling_;

    unsigned long trade_interval_ms_ = 100;
    unsigned long liquidity_interval_ms_ = 100;

    double profit_margin_;
    double last_price_;
    double prev_change_;

    int last_client_order_id_;
    std::optional<int> last_accepted_order_id_;
    std::optional<MarketDataPtr> last_market_data_;
    std::optional<CustomerOrderMessagePtr> current_customer_order_;
    std::stack<CustomerOrderMessagePtr> customer_orders_; // Latest customer order 

    unsigned long next_lower_margin_timestamp_;
    unsigned long next_undercut_timestamp_;

    std::mt19937 random_generator_;

    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;

    constexpr static double C_A = 0.05;
    constexpr static double C_R = 0.05;
    constexpr static double REL_JITTER = 0.25;

    constexpr static unsigned long MS_TO_NS = 1000000;
};

#endif