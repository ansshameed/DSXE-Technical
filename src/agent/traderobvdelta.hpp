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
        next_trade_timestamp_ = timeNow() + (trade_interval_ms_ * MS_TO_NS);
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
                if (timeNow() >= next_trade_timestamp_)
                {
                    if (close_prices_.size() >= lookback_length_ && volumes_.size() >= lookback_length_)
                    {
                        std::vector<double> delta_obv_values = calculateOBVDelta();  

                        if (!delta_obv_values.empty())  
                        {
                            double delta_obv = delta_obv_values.back();  
                            placeOrder(delta_obv);
                        }
                        else
                        {
                            std::cerr << "ERROR: OBV Delta vector is empty! No order placed.\n";
                        }
                    }
                    else
                    {
                        std::cout << "Not enough data to place an order.\n";
                    }

                    next_trade_timestamp_ = timeNow() + (trade_interval_ms_ * MS_TO_NS);
                }
                sleep();
                lock.lock();
            }
            lock.unlock();
        });
    }

    void reactToMarket(MarketDataMessagePtr msg)
    {
        if (!msg || !msg->data)
        {
            std::cout << "[ERROR] Invalid market data received.\n";
            return;
        }

        double price = msg->data->last_price_traded;
        double volume = msg->data->last_quantity_traded;

        if (price <= 0 || volume < 0)
        {
            std::cout << "[ERROR] Invalid market price or volume received.\n";
            return;
        }

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
        if (!last_market_data_.has_value() || last_market_data_.value()->best_bid <= 0 || last_market_data_.value()->best_ask <= 0)
        {
            std::cout << "No valid bid/ask data, skipping order placement.\n";
            return;
        }

        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, trader_side_, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }

        int quantity = 100;
        double best_bid = last_market_data_.value()->best_bid;
        double best_ask = last_market_data_.value()->best_ask;

        if ((delta_obv > threshold_ && trader_side_ == Order::Side::BID) || (delta_obv < -threshold_ && trader_side_ == Order::Side::ASK))
        {
            double price = getQuotePrice(delta_obv, best_bid, best_ask, trader_side_);
            placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
        }
        else
        {
            std::cout << "Trade conditions NOT met. No order placed.\n";
        }
    }


    double getQuotePrice(double delta_obv, double best_bid, double best_ask, Order::Side trader_side_)
    {
        double price;
        double slippage = getRandom(-1, 1);

        if (trader_side_ == Order::Side::BID)
        {
            if (delta_obv > threshold_ && best_bid < (best_ask - 1))
            {
                price = best_bid + 1 + slippage;
            }
            else
            {
                price = best_bid + slippage;
            }
            return std::min(price, limit_price_);
        }
        else
        {
            if (delta_obv < -threshold_ && best_ask > (best_bid + 1))
            {
                price = best_ask - 1 + slippage;
            }
            else
            {
                price = best_ask + slippage;
            }
            return std::max(price, limit_price_);
        }
    }


    std::vector<double> calculateOBVDelta() 
    {   

        size_t n = close_prices_.size(); 
        if (n < lookback_length_ || volumes_.size() < lookback_length_) { 
            return {}; 
        }

        std::vector<double> output(n, 0.0); // Initialise close prices with 0
        size_t front_bad = lookback_length_; // First index to calculate OBV Delta

        // Ensure we have valid starting volume data
        for (size_t first_volume = 0; first_volume < n; first_volume++) { // Loop through all volume ticks to find first non-zero volume. 
            if (volumes_[first_volume] > 0) { // If volume greater than zero (found first valid volume tick), set front_bad to this index and break loop
                break;
            }
            front_bad = std::min(front_bad, n - 1); // If volume is zero, set front_bad to n - 1 (last index); OBV computation will be skipped 
        }

        for (size_t i = 0; i < front_bad; i++) { // Set all values before front_bad to 0.0 because OBV calculations cannot be performed without valid volume data
            output[i] = 0.0;
        }

        for (size_t icase = front_bad; icase < n; icase++) { // Starts iterating from front_bad (where volume data is valid) up to n (close prices size)

            double signed_volume = 0.0; // Signed volume = volume weighted by price movements (positive if price increase, negative if price decrease)
            double total_volume = 0.0; // Total volume = sum of all volumes in lookback period (regardless of price direction)

            for (size_t i = 1; i < lookback_length_ && (icase - i) > 0; i++) {  // Loop backwards for lookback_length periods. 
                if (icase - i - 1 < 0) { // Prevents accessing out-of-bounds index
                    std::cerr << "ERROR: Out-of-bounds access prevented (icase - i - 1)" << std::endl;
                    break;
                }

                if (close_prices_[icase - i] > close_prices_[icase - i - 1]) { // If closing price is greater than previous closing price, add volume to signed volume
                    signed_volume += volumes_[icase - i];
                } 
                else if (close_prices_[icase - i] < close_prices_[icase - i - 1]) { // If price decreased then subtract the volume
                    signed_volume -= volumes_[icase - i];
                }
                total_volume += volumes_[icase - i]; // Add volume to total volume
            }

            if (total_volume <= 0.0) { // Normalise OBV Value; If total volume is zero or negative (no trades in lookback window), set output to 0.0
                output[icase] = 0.0;
                continue;
            }

            double value = signed_volume / total_volume; // Calculate OBV ratio
            double normalized_value = 100.0 * std::erfc(-0.6 * value * sqrt(static_cast<double>(lookback_length_))) - 50.0; // Normalise OBV value to 0-100 scale. Error function erfc to smoothen OBV. High positive OBV delta = strong buying pressure; negative = selling pressure
            output[icase] = normalized_value;
        }

        if (n < front_bad + delta_length_) { // If no. of closing prices less than front_bad + delta_length then log error and return
            std::cerr << "ERROR: Not enough data for delta calculation!" << std::endl; // Ensures atleast delta_length values are available for delta calculation
            return output;
        }

        for (int icase = static_cast<int>(n) - 1; icase >= static_cast<int>(front_bad + delta_length_); icase--) { // Loop backwards from last index to front_bad + delta_length to compute OBV delta over delta_length
            if (icase - delta_length_ < 0) { // Prevents accessing out-of-bounds index 
                std::cerr << "ERROR: Out-of-bounds access prevented (icase - delta_length)\n";
                break;
            }
            output[icase] -= output[icase - delta_length_]; // Subtract OBV value from delta length preiods ago to compute OBV change
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
};

#endif