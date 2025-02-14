#ifndef TRADER_VWAP_OBV_DELTA_HPP
#define TRADER_VWAP_OBV_DELTA_HPP

#include <vector>
#include <numeric>
#include <algorithm>
#include "traderagent.hpp"

class TraderVWAPOBVDelta : public TraderAgent
{
public:
    TraderVWAPOBVDelta(NetworkEntity *network_entity, TraderConfigPtr config, int lookback_vwap, int lookback_obv, int delta_length, double threshold)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      lookback_vwap_{lookback_vwap},
      lookback_obv_{lookback_obv},
      delta_length_{delta_length},
      threshold_{threshold},
      cancelling_{config->cancelling},
      trade_interval_ms_{config->trade_interval},
      random_generator_{std::random_device{}()},
      mutex_{}
    {
        // Connect to exchange and subscribe to market data
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Start with a delayed execution
        addDelayedStart(config->delay);
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
        std::cout << "Last price traded: " << msg->data->last_price_traded << "\n";

        double closing_price = msg->data->last_price_traded; // Closing price is the last price traded
        double volume = msg->data->volume_per_tick; // Volume is the last quantity traded

        // Store the most recent prices & volumes for VWAP and OBV Delta calculation
        price_volume_data_.emplace_back(closing_price, volume); // Store the most recent prices & volumes for rolling VWAP calculation
        close_prices_.push_back(closing_price); // Store the most recent closing prices for OBV Delta calculation
        volumes_.push_back(volume); // Store the most recent volumes for OBV Delta calculation

        if (price_volume_data_.size() > lookback_vwap_) // If the lookback period is exceeded, remove the oldest price-volume data
        {
            price_volume_data_.erase(price_volume_data_.begin());
        }
 
        if (close_prices_.size() > lookback_obv_) // If the lookback period is exceeded, remove the oldest closing price and volume
        {
            close_prices_.erase(close_prices_.begin());
            volumes_.erase(volumes_.begin());
        }

        // Ensure there is enough data for VWAP calculation
        if (price_volume_data_.size() < lookback_vwap_)
        {
            std::cerr << "ERROR: Not enough data for VWAP calculation!" << std::endl;
            return;
        }

        // Ensure there is enough data for OBV Delta calculation
        if (close_prices_.size() < lookback_obv_)
        {
            std::cerr << "ERROR: Not enough data for OBV Delta calculation!" << std::endl;
            return;
        }

        // Calculate VWAP
        double rolling_vwap = calculateVWAP(price_volume_data_); // Calculate the VWAP using the price-volume data (rolling VWAP window)
        std::cout << "Rolling VWAP: " << rolling_vwap << "\n";

        // Calculate OBV Delta
        auto delta_obv = calculateDeltaOBV(close_prices_, volumes_, lookback_obv_, delta_length_); // Calculate the OBV Delta using the closing prices and volumes

        if (delta_obv.empty()) // If OBV Delta is empty, return
        {
            std::cerr << "ERROR: delta_obv is empty!" << std::endl;
            return;
        }

        double latest_delta_obv = delta_obv.back(); // Get the most recent OBV Delta value
        std::cout << "Latest Delta OBV: " << latest_delta_obv << "\n";

        // Implement trading logic based on VWAP and OBV Delta
        if (trader_side_ == Order::Side::BID && closing_price < rolling_vwap && latest_delta_obv > threshold_) // If trader is BID and price is below VWAP and Delta OBV above threshold, place BID order
        {
            std::cout << "Price below VWAP and Delta OBV > threshold, placing BID order\n";
            placeOrder(Order::Side::BID, rolling_vwap);
        }
        else if (trader_side_ == Order::Side::ASK && closing_price > rolling_vwap && latest_delta_obv < -threshold_) // If trader is ASK and price is above VWAP and Delta OBV below negative threshold, place ASK order
        {
            std::cout << "Price above VWAP and Delta OBV < -threshold, placing ASK order\n";
            placeOrder(Order::Side::ASK, rolling_vwap);
        }
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        if (msg->order->status == Order::Status::NEW)
        {
            last_accepted_order_id_ = msg->order->id;
        }

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
        std::cout << "Total Profit: " << std::fixed << std::setprecision(0) << total_profit_ << "\n"; 
    }

private:

    void sendProfitToExchange()
    {
        ProfitMessagePtr profit_msg = std::make_shared<ProfitMessage>();
        profit_msg->agent_id = this->agent_id;
        profit_msg->agent_name = agent_name_;
        profit_msg->profit = total_profit_;
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

    double calculateVWAP(const std::vector<std::pair<double, double>>& data)
    {
        double price_volume_sum = 0.0;
        double volume_sum = 0.0;

        for (const auto& [price, volume] : data) // For each price-volume pair in the data
        {
            price_volume_sum += price * volume; // Calculate the sum of price * volume
            volume_sum += volume; // Calculate the sum of volume
        }

        return volume_sum > 0 ? price_volume_sum / volume_sum : 0.0; // Calculate the VWAP
    }

    std::vector<double> calculateDeltaOBV(const std::vector<double>& close_prices, 
                                          const std::vector<double>& volumes, 
                                          int lookback_length, 
                                          int delta_length)
    {   

        size_t n = close_prices.size();
        if (n < lookback_length || volumes.size() < lookback_length) { // If no. of prices or volume ticks less than lookback length then throw exception
            throw std::invalid_argument("Insufficient data for the given lookback length.");
        }

        std::vector<double> output(n, 0.0); // Create vector of zeros with size n (no. of closing prices)
        size_t front_bad = lookback_length; // Set front_bad to lookback length

        for (size_t first_volume = 0; first_volume < n; first_volume++) { // Loop through closing prices
            if (volumes[first_volume] > 0) { // If volume is greater than zero, break
                break;
            }
            front_bad = std::min(front_bad, n - 1); // If volume is zero, set front_bad to n - 1 (last index); OBV computation will be skipped
        }

        for (size_t i = 0; i < front_bad; i++) { // Set all values before front_bad to 0.0
            output[i] = 0.0;
        }

        for (size_t icase = front_bad; icase < n; icase++) { // Loop through closing prices
            double signed_volume = 0.0;
            double total_volume = 0.0;

            for (size_t i = 1; i < lookback_length && (icase - i) > 0; i++) { // Loop backwards for lookback_length periods
                if (icase - i - 1 < 0) {
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

            if (total_volume <= 0.0) { // If total volume is zero or negative, set output to 0.0
                output[icase] = 0.0;
                continue;
            }

            double value = signed_volume / total_volume; // Calculate OBV ratio
            double normalized_value = 100.0 * std::erfc(-0.6 * value * sqrt(static_cast<double>(lookback_length))) - 50.0; // Normalise OBV value to 0-100 scale
            output[icase] = normalized_value; // Store the normalised OBV value
        }

        if (n < front_bad + delta_length) { // If no. of closing prices less than front_bad + delta_length then log error and return
            std::cerr << "ERROR: Not enough data for delta calculation!" << std::endl;
            return output;
        }
 
        for (int icase = static_cast<int>(n) - 1; icase >= static_cast<int>(front_bad + delta_length); icase--) { // Loop backwards from last index to front_bad + delta_length to compute OBV delta over delta_length
            if (icase - delta_length < 0) {
                std::cerr << "ERROR: Out-of-bounds access prevented (icase - delta_length)\n";
                break;
            }
            output[icase] -= output[icase - delta_length]; // Subtract OBV value from delta length preiods ago to compute OBV change
        }

        return output;
    }

    void placeOrder(Order::Side side, double vwap_price)
    {
        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, side, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }

        int quantity = getRandomOrderSize();

        double price_adjustment = 0.001 * vwap_price; // 0.1% price adjustment for slippage
        double price = (side == Order::Side::BID) ? (vwap_price + price_adjustment) // If BID then increase price slightly, else decrease price slightly (ASK)
                                                  : (vwap_price - price_adjustment);

        placeLimitOrder(exchange_, side, ticker_, quantity, price, limit_price_);

        std::cout << ">> " << (side == Order::Side::BID ? "BID" : "ASK") 
                  << " " << quantity << " @ " << price << "\n";
    }

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    int lookback_vwap_;
    int lookback_obv_;
    int delta_length_;
    double threshold_;
    bool cancelling_;
    unsigned int trade_interval_ms_;
    std::vector<std::pair<double, double>> price_volume_data_;
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
    std::string agent_name_ = "OBV & VWAP";
};

#endif