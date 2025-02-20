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
        is_trading_ = false;
        std::cout << "Trading window ended.\n"; 
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

        if (!msg || !msg->data) { // Check if exchange message received contains valid data (not null)
            std::cerr << "ERROR: Received null market data message!" << std::endl;
            return;
        }
        
        close_prices_.push_back(msg->data->last_price_traded); // Add the last traded price to the list of prices (close prices) 
        volumes_.push_back(msg->data->volume_per_tick); // Add the volume traded in the last tick to the list of volumes
        std::cout << "DEBUG: Current Volume: " << msg->data->volume_per_tick << "\n";

        if (close_prices_.size() < lookback_length_ || volumes_.size() < lookback_length_) { // Ensures no. of close prices and volume ticks as long as lookback length
            std::cerr << "WARNING: Not enough data yet! Needed: " << lookback_length_ // If not enough data then log warning and return
                    << ", Current: " << close_prices_.size() << std::endl;
            return;
        }

        if (volumes_.empty()) { // Skip order when no volume 
        std::cerr << "Skipping order: No volume data available." << std::endl;
        return;
        }

        if (volumes_.back() == 0 && volumes_.size() > 1) { // If volume == 0 then use last non-zero volume 
            std::cerr << "Using last nonzero volume: " << volumes_[volumes_.size() - 2] << "\n";
            volumes_.back() = volumes_[volumes_.size() - 2]; // Use last valid volume
        }

        //delta = Change in OBV over specified period (delta_length)
        auto delta_obv = calculateDeltaOBV(close_prices_, volumes_, lookback_length_, delta_length_); // Calculate OBV Delta 

        if (delta_obv.empty()) { // If OBV Delta empty then log error and return. Order not placed
            std::cerr << "ERROR: delta_obv is empty!" << std::endl;
            return;
        }

        // Threshold = Dynamically adjusted OBV Delta limit to determine when trade should be executed. Lower threshold = more trades (more sensitive) 
        threshold_ = std::max(50.0, threshold_ * (1.0 - 0.02 * (volumes_.back() / 100.0))); // Adjust threshold dynamically based on last trading volume. Decreases slightly when volume increases, never goes below 50. 

        double latest_delta_obv = delta_obv.back(); 

        if (latest_delta_obv > threshold_ && trader_side_ == Order::Side::BID) // If OBV Delta above threshold, place bid
        {
            placeOrder(Order::Side::BID);
        }
        else if (latest_delta_obv < -threshold_ && trader_side_ == Order::Side::ASK) // If OBV Delta below negative threshold, place ask
        {
            placeOrder(Order::Side::ASK);
        }

    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        if (msg->order->status == Order::Status::NEW)
        {
            last_accepted_order_id_ = msg->order->id;
        }

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
                sleep();
                lock.lock();
            }
            lock.unlock();
            std::cout << "Finished actively trading.\n";
        });
    }

    void sleep()
    {
        std::uniform_real_distribution<> dist(-REL_JITTER, REL_JITTER);
        unsigned long jitter = dist(random_generator_);
        unsigned long sleep_time_ms = std::round(trade_interval_ms_ * (1.0 + jitter));
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
    }

    std::vector<double> calculateDeltaOBV(const std::vector<double>& close_prices, 
                                      const std::vector<double>& volumes, 
                                      int lookback_length, 
                                      int delta_length)
    {   
        size_t n = close_prices.size(); // No. of closing prices (ticks)
        if (n < lookback_length || volumes.size() < lookback_length) { // If no. of prices or volume ticks less than lookback length then throw exception
            throw std::invalid_argument("Insufficient data for the given lookback length.");
        }

        std::vector<double> output(n, 0.0); // Create vector of zeros with size n (no. of closing prices)
        size_t front_bad = lookback_length; // Stores first index where OBV can be calculated (initially set to lookback length). Ensures calculation can skip leading zero-volume entires and start from first valid volume data

        // Ensure we have valid starting volume data
        for (size_t first_volume = 0; first_volume < n; first_volume++) { // Loop through all volume ticks to find first non-zero volume. 
            if (volumes[first_volume] > 0) { // If volume greater than zero (found first valid volume tick), set front_bad to this index and break loop
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

            for (size_t i = 1; i < lookback_length && (icase - i) > 0; i++) {  // Loop backwards for lookback_length periods. 
                if (icase - i - 1 < 0) { // Prevents accessing out-of-bounds index
                    std::cerr << "ERROR: Out-of-bounds access prevented (icase - i - 1)" << std::endl;
                    break;
                }

                if (close_prices[icase - i] > close_prices[icase - i - 1]) { // If closing price is greater than previous closing price, add volume to signed volume
                    signed_volume += volumes[icase - i];
                } else if (close_prices[icase - i] < close_prices[icase - i - 1]) { // If price decreased then subtract the volume
                    signed_volume -= volumes[icase - i];
                }
                total_volume += volumes[icase - i]; // Add volume to total volume
            }

            if (total_volume <= 0.0) { // Normalise OBV Value; If total volume is zero or negative (no trades in lookback window), set output to 0.0
                output[icase] = 0.0;
                continue;
            }

            double value = signed_volume / total_volume; // Calculate OBV ratio
            double normalized_value = 100.0 * std::erfc(-0.6 * value * sqrt(static_cast<double>(lookback_length))) - 50.0; // Normalise OBV value to 0-100 scale. Error function erfc to smoothen OBV. High positive OBV delta = strong buying pressure; negative = selling pressure
            output[icase] = normalized_value;
        }

        if (n < front_bad + delta_length) { // If no. of closing prices less than front_bad + delta_length then log error and return
            std::cerr << "ERROR: Not enough data for delta calculation!" << std::endl; // Ensures atleast delta_length values are available for delta calculation
            return output;
        }

        for (int icase = static_cast<int>(n) - 1; icase >= static_cast<int>(front_bad + delta_length); icase--) { // Loop backwards from last index to front_bad + delta_length to compute OBV delta over delta_length
            if (icase - delta_length < 0) { // Prevents accessing out-of-bounds index 
                std::cerr << "ERROR: Out-of-bounds access prevented (icase - delta_length)\n";
                break;
            }
            output[icase] -= output[icase - delta_length]; // Subtract OBV value from delta length preiods ago to compute OBV change
        }

        return output;
    }

    void placeOrder(Order::Side side)
    {
        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, side, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }
        //int quantity = 100; 
        int quantity = getRandomOrderSize(); 
        double price = getQuotePrice(side);
        placeLimitOrder(exchange_, side, ticker_, quantity, price, limit_price_);

        std::cout << ">> " << (side == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << "\n";
    }

    double getQuotePrice(Order::Side side)
    {
        if (close_prices_.empty()) return limit_price_;  
        
        double price = close_prices_.back();
        
        // Apply small variation based on OBV strength
        double price_adjustment = std::min(5.0, std::abs(threshold_ / 1000.0)); // Example tweak
        if (side == Order::Side::BID)
        {
            price = std::min(price + price_adjustment, limit_price_);
        }
        else
        {
            price = std::max(price - price_adjustment, limit_price_);
        }
        return price;
    }

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    bool cancelling_;
    unsigned int trade_interval_ms_;
    int lookback_length_;
    int delta_length_;
    double threshold_; 
    std::vector<double> close_prices_;
    std::vector<double> volumes_;
    std::optional<int> last_accepted_order_id_ = std::nullopt;
    std::mt19937 random_generator_;
    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;
    constexpr static double REL_JITTER = 0.25;

    // Profitability parameters 
    struct Trade { 
        double price;
        int quantity;
        Order::Side side;
    }; 
    std::vector<Trade> executed_trades_; 
    double total_profit_ = 0.0;
};

#endif