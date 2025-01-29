#ifndef TRADER_MACD_HPP
#define TRADER_MACD_HPP

#include "traderagent.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

class TraderMACD : public TraderAgent
{
public:
    TraderMACD(NetworkEntity *network_entity, TraderConfigPtr config, int short_length, int long_length, int signal_length, double threshold, int n_to_smooth, size_t lookback_period)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      cancelling_{config->cancelling},
      trade_interval_ms_{config->trade_interval},
      short_length_{short_length},
      long_length_{long_length},
      signal_length_{signal_length},
      threshold_{threshold},
      n_to_smooth_{n_to_smooth},
      lookback_period_{lookback_period},
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
        //std::cout << "High Price: " << msg->data->high_price << ", Low Price: " << msg->data->low_price << "\n";
        
        prices_.push_back(msg->data->last_price_traded); // Add the last traded price to the list of prices
        highs_.push_back(msg->data->high_price); // Stores the high price for the current time period 
        lows_.push_back(msg->data->low_price); // Stores the low price for the current time period
        closes_.push_back(msg->data->last_price_traded); // Stores the last traded price for the current time period (closing price)

        updateRollingWindow(msg->data->high_price, msg->data->low_price); // Update the rolling window with the high and low prices for ATR calculations

        if (prices_.size() >= long_length_) // If there are enough prices to calculate MACD (long length is minimum number of datapoints required for calculation; slow EMA)
        {
            auto [macd_line, signal_line] = calculateMACD(); // Calculate MACD and signal line
            double macd = macd_line.back(); // Get the most recent MACD value
            double signal = signal_line.back(); // Get the most recent signal line value
            double histogram = macd - signal; // Calculate the histogram as difference between MACD and signal line (indicates divergence or convergence between MACD and signal line)

            std::cout << "MACD: " << macd << ", Signal: " << signal << ", Histogram: " << histogram << "\n";

            if (histogram > threshold_ && trader_side_ == Order::Side::BID) // If histogram is greater than threshold and trader is a buyer, buy signal is generated
            {   
                //std::cout << "Buy signal detected. Placing BID order.\n";
                placeOrder(Order::Side::BID);
            } 
            else if (histogram < -threshold_ && trader_side_ == Order::Side::ASK) // If histogram is less than negative threshold and trader is a seller, sell signal is generated
            {   
                //std::cout << "Sell signal detected. Placing ASK order.\n";
                placeOrder(Order::Side::ASK);
            }
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

    void updateRollingWindow(double high_price, double low_price) { 
        high_window_.push_back(high_price); // Add high price to the high price window
        low_window_.push_back(low_price); // Add low price to the low price window
        if (high_window_.size() > lookback_period_) { // If the window size is greater than the lookback period, remove the oldest value (ensures window size = lookback)
            high_window_.pop_front(); // Remove the oldest high price
            low_window_.pop_front(); // Remove the oldest low price
        }
    }

    std::pair<std::vector<double>, std::vector<double>> calculateMACD()
    {   
        // Store MACD and signal line values for each price point
        std::vector<double> macd_line(prices_.size(), 0.0); // Initialise MACD line with zeros 
        std::vector<double> signal_line(prices_.size(), 0.0); // Initialise signal line with zeros

        double long_sum = prices_[0]; // Initialise long EMA sum with first price (26-period)
        double short_sum = prices_[0]; // Initialise short EMA sum with first price (12-period)

        double long_alpha = 2.0 / (long_length_ + 1.0); // Calculate alpha for long EMA
        double short_alpha = 2.0 / (short_length_ + 1.0);  // Calculate alpha for short EMA
        double signal_alpha = 2.0 / (signal_length_ + 1.0); // Calculate alpha for signal line (9-period)

        // Calculate MACD line and EMAs for each price point. 
        for (size_t icase = 1; icase < prices_.size(); ++icase) // Loop through each price point
        {
            long_sum = long_alpha * prices_[icase] + (1.0 - long_alpha) * long_sum; // Calculate long EMA
            short_sum = short_alpha * prices_[icase] + (1.0 - short_alpha) * short_sum; // Calculate short EMA
            double diff = 0.5 * (long_length_ - 1.0) - 0.5 * (short_length_ - 1.0); // Calculate difference between long and short EMA lengths

            double denom = std::sqrt(std::abs(diff)); // Normalisation factor for MACD using EMA difference
            denom *= ATR(icase, lookback_period_); // Normalisation factor scaled by ATR to account for volatility

            macd_line[icase] = (short_sum - long_sum) / (denom + 1.e-15); // MACD normalisation 
            macd_line[icase] = 100.0 * normal_cdf(1.0 + macd_line[icase]) - 50.0; // Normalise MACD to 0-100 scale (applies normal cumulative distributin function to MACD; CDF) 

            //std::cout << "Price[" << icase << "]: " << prices_[icase] << ", Short EMA: " << short_sum 
                      //<< ", Long EMA: " << long_sum << ", MACD: " << macd_line[icase] << "\n";
        }

        signal_line[0] = macd_line[0]; // Calculate the signal line as an EMA of the MACD line
        for (size_t icase = 1; icase < macd_line.size(); ++icase) // Loop through each price point
        {
            signal_line[icase] = signal_alpha * macd_line[icase] + (1.0 - signal_alpha) * signal_line[icase - 1]; // Calculate signal line as EMA of MACD line using signal_alpha as smoothing factor
        }

        if (n_to_smooth_ > 1) // If additional smoothing steps are required
        {
            double alpha = 2.0 / (n_to_smooth_ + 1.0); // Calculate alpha for additional smoothing
            double smoothed = macd_line[0]; // Initialise smoothed MACD line with first MACD value
            for (size_t icase = 1; icase < macd_line.size(); ++icase) // Loop through each price point
            {
                smoothed = alpha * macd_line[icase] + (1.0 - alpha) * smoothed; // Apply additional smoothing to MACD line
                macd_line[icase] -= smoothed; // Subtract smoothed MACD line from MACD line
            }
        }

        return {macd_line, signal_line}; // Return MACD line and signal line
    }


    double ATR(size_t end, size_t lookback)
    {
        if (end < lookback) { // If not enough values to calculate ATR
            lookback = end + 1; // Adjust lookback if not enough values to include all available data points 
        }
    
        double atr = 0.0; // Initialise ATR with zero (accumulate total true range over lookback period)
        size_t start = end - lookback + 1; // Calculate start index for ATR calculation for rolling window 

        for (size_t i = start; i <= end; ++i) // Loop through rolling window to calculate the TR (True Range) for each period and accumulate total ATR
        {
            //high_window_[i - start] = highs_[i]; = high price for the current time period, low_window_[i - start] = lows_[i]; = low price for the current time period, closes_[i - 1] = closes_[i - 1]; = closing price for the previous time period
            double high_low = high_window_[i - start] - low_window_[i - start]; // High-low range for the current time period (difference between high and low prices for current period)
            double high_close = std::abs(high_window_[i - start] - closes_[i - 1]); // Abs. difference between current high price and previous closing price
            double low_close = std::abs(low_window_[i - start] - closes_[i - 1]); // Abs. difference between current low price and previous closing price
            atr += std::max({high_low, high_close, low_close}); // Calculate the True Range (TR) for each period and accumulate total ATR
            //std::cout << "ATR: " << atr << "\n";
        }

        return atr / lookback; // Return the average ATR over the lookback period
    }

    // Calculate the normal CDF for given value 
    double normal_cdf(double value)
    {  
        return 0.5 * std::erfc(-value / std::sqrt(2.0)); // Gaussian CDF (error function) for normal distribution. Result scaled by 0.5 to normalise to 0-1 scale
    } 

    void placeOrder(Order::Side side)
    {
        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, side, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }

        //int quantity = 100;
        int quantity = getRandomOrderSize(); // Use random order size
        double price = getQuotePrice(side);
        placeLimitOrder(exchange_, side, ticker_, quantity, price, limit_price_);

        std::cout << ">> " << (side == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << "\n";
    }

    double getQuotePrice(Order::Side side)
    {
        double price = prices_.back();
        if (side == Order::Side::BID)
        {
            price = std::min(price, limit_price_);
        }
        else
        {
            price = std::max(price, limit_price_);
        }
        return price;
    }

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    bool cancelling_;
    unsigned int trade_interval_ms_;
    int short_length_;
    int long_length_;
    int signal_length_;
    double threshold_;
    int n_to_smooth_;
    std::vector<double> prices_;
    std::vector<double> highs_;
    std::vector<double> lows_;
    std::vector<double> closes_;
    std::deque<double> high_window_;
    std::deque<double> low_window_;
    size_t lookback_period_;
    std::optional<int> last_accepted_order_id_ = std::nullopt;
    std::mt19937 random_generator_;
    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;
    constexpr static double REL_JITTER = 0.25;
};

#endif