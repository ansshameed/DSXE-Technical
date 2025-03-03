#ifndef TRADER_OBVDELTA_HPP
#define TRADER_OBVDELTA_HPP

#include "traderagent.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

class TraderOBVDelta : public TraderAgent
{
public:
    TraderOBVDelta(NetworkEntity *network_entity, TraderConfigPtr config, int lookback_length, int delta_length, double threshold)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      cancelling_{config->cancelling},
      trade_interval_ms_{config->trade_interval},
      lookback_length_{lookback_length},
      delta_length_{delta_length},
      threshold_{threshold}, 
      random_generator_{std::random_device{}()},
      mutex_{}
    {
        // Mark as legacy agent
        is_legacy_trader_ = false;

        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);
    }

    std::string getAgentName() const override { return "OBV Delta"; }

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

    }

    void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) override
    {
        std::cout << "Received cancel reject from " << exchange << ": Order: " << msg->order_id;
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
                std::cout << "[OBV Delta] Enqueued CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
            }
            return; // Just exit the function, don’t call base handler
        }

        // If it's not a customer order, call the base class handler
        TraderAgent::handleBroadcastFrom(sender, message);
    }

private:

    double getRandom(double lower, double upper)
    {
        std::uniform_real_distribution<> dist(lower, upper);
        return dist(random_generator_);
    }

    void sendProfitToExchange()
    {
        ProfitMessagePtr profit_msg = std::make_shared<ProfitMessage>();
        profit_msg->agent_name = getAgentName(); 
        profit_msg->profit = balance; 
        sendMessageTo(exchange_, std::dynamic_pointer_cast<Message>(profit_msg), true);
    }   

    void activelyTrade()
    {
        trading_thread_ = new std::thread([&, this]()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (is_trading_)
            {
                lock.unlock();
                if (close_prices_.size() >= lookback_length_ && volumes_.size() >= lookback_length_)
                {
                    std::vector<double> delta_obv_values = calculateOBVDelta();  
                    placeOrder(delta_obv_values.back());
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
        double price = msg->data->last_price_traded;
        double volume = msg->data->last_quantity_traded;

        close_prices_.push_back(price);
        volumes_.push_back(volume);

        if (close_prices_.size() > lookback_length_)
            close_prices_.erase(close_prices_.begin());

        if (volumes_.size() > lookback_length_)
            volumes_.erase(volumes_.begin());

        last_market_data_ = msg->data;
    } 

    void placeOrder(double delta_obv)
    {
            if (cancelling_ && last_accepted_order_id_.has_value())
            {
                cancelOrder(exchange_, trader_side_, ticker_, last_accepted_order_id_.value());
                last_accepted_order_id_ = std::nullopt;
            }
        
            if (!customer_orders_.empty()) 
            {   
                auto cust_order = customer_orders_.top(); // Get next customer order
                customer_orders_.pop();
                limit_price_ = cust_order->price;
            }
        
            std::uniform_int_distribution<int> dist(10, 50);  // Generates integers between 10 and 50
            int quantity = dist(random_generator_);
            double best_bid = last_market_data_.value()->best_bid;
            double best_ask = last_market_data_.value()->best_ask;
            double last_price = last_market_data_.value()->last_price_traded;

            bool should_place_order = false;
            if (trader_side_ == Order::Side::BID && delta_obv > threshold_) 
            {
                should_place_order = true;
            }
            else if (trader_side_ == Order::Side::ASK && delta_obv < -threshold_)
            {
                should_place_order = true;
            }
            
            if (should_place_order) 
            { 
                double price = getQuotePrice(last_price, delta_obv, best_bid, best_ask); 
                placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
                std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << " | OBV Delta: " << delta_obv << " | Threshold: " << threshold_ << "\n";
            }
            else 
            { 
                std::cout << "Trade conditions NOT met. No order placed.\n";
            }
    }

    double getQuotePrice(double last_price, double delta_obv, double best_bid, double best_ask)
    {   
       double candidate_price = 0.0; 
       
       if (trader_side_ == Order::Side::BID)
       {
          candidate_price = best_ask; // Immediately buy by lifting the ask 
          candidate_price = std::min(limit_price_, candidate_price); // Ensure price is not higher than the limit price
       } 
       else 
       { 
            candidate_price = best_bid; // Immediately sell by hitting the bid
            candidate_price = std::max(limit_price_, candidate_price); // Ensure price is not lower than the limit price
       }

       return candidate_price;
    }


    std::vector<double> calculateOBVDelta() 
    {   
        size_t n = close_prices_.size(); 
        if (n < lookback_length_ || volumes_.size() < lookback_length_) { 
            return {}; 
        }

        std::vector<double> output(n, 0.0);
        size_t front_bad = lookback_length_;

        // More lenient volume check
        for (size_t first_volume = 0; first_volume < n; first_volume++) {
            if (volumes_[first_volume] > 0) {
                break;
            }
            front_bad = std::min(front_bad, n - 1);
        }

        for (size_t i = 0; i < front_bad; i++) {
            output[i] = 0.0;
        }

        for (size_t icase = front_bad; icase < n; icase++) {
            double signed_volume = 0.0;
            double total_volume = 0.0;

            for (size_t i = 1; i < lookback_length_ && (icase - i) > 0; i++) {
                if (icase - i - 1 < 0) {
                    std::cerr << "ERROR: Out-of-bounds access prevented (icase - i - 1)" << std::endl;
                    break;
                }

                // More lenient price change detection with a small threshold
                double price_change_threshold = 0.001; // 0.1% price change
                if (std::abs(close_prices_[icase - i] - close_prices_[icase - i - 1]) / close_prices_[icase - i - 1] > price_change_threshold) {
                    // Price change is significant
                    if (close_prices_[icase - i] > close_prices_[icase - i - 1]) {
                        signed_volume += volumes_[icase - i];
                    } 
                    else {
                        signed_volume -= volumes_[icase - i];
                    }
                }
                total_volume += volumes_[icase - i];
            }

            if (total_volume <= 0.0) {
                output[icase] = 0.0;
                continue;
            }

            // More aggressive normalization
            double value = signed_volume / total_volume;
            // Adjusted normalization to make signals more frequent
            double normalized_value = 200.0 * std::tanh(value * sqrt(static_cast<double>(lookback_length_))) - 100.0;
            
            // Add debug logging
            std::cout << "OBV Calculation Debug:"
                    << " icase=" << icase 
                    << " signed_volume=" << signed_volume 
                    << " total_volume=" << total_volume 
                    << " value=" << value 
                    << " normalized_value=" << normalized_value << std::endl;

            output[icase] = normalized_value;
        }

        if (n < front_bad) {
            std::cerr << "ERROR: Not enough data for delta calculation!" << std::endl;
            return output;
        }

        // More flexible delta calculation
        for (int icase = static_cast<int>(n) - 1; icase >= static_cast<int>(front_bad); icase--) {
            if (icase - delta_length_ < 0) {
                std::cerr << "ERROR: Out-of-bounds access prevented (icase - delta_length)\n";
                break;
            }
            // Use absolute delta for more trading opportunities
            output[icase] = std::abs(output[icase] - output[icase - delta_length_]);
        }

        return output;
    }

    void sleep()
    {
        std::uniform_real_distribution<> dist(-REL_JITTER, REL_JITTER);
        unsigned long jitter = dist(random_generator_);
        unsigned long sleep_time_ms = std::round(trade_interval_ms_ * (1.0 + jitter));
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
    }

    unsigned long long timeNow()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    int lookback_length_;
    int delta_length_;
    double threshold_;
    bool cancelling_;
    unsigned int trade_interval_ms_;
    std::vector<double> close_prices_;
    std::vector<double> volumes_;
    std::optional<int> last_accepted_order_id_ = std::nullopt;
    std::mt19937 random_generator_;
    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;
    std::optional<MarketDataPtr> last_market_data_;
    constexpr static double REL_JITTER = 0.25;
    constexpr static unsigned long MS_TO_NS = 1000000;
    unsigned long next_trade_timestamp_;
    std::stack<CustomerOrderMessagePtr> customer_orders_;
};

#endif