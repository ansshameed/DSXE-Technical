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

            //std::cout << "MACD: " << macd << ", Signal: " << signal << ", Histogram: " << histogram << "\n";

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
        high_window_.push_back(high_price); 
        low_window_.push_back(low_price);
        if (high_window_.size() > lookback_period_) {
            high_window_.pop_front();
            low_window_.pop_front();
        }
    }

    std::pair<std::vector<double>, std::vector<double>> calculateMACD()
    {
        std::vector<double> macd_line(prices_.size(), 0.0);
        std::vector<double> signal_line(prices_.size(), 0.0);
        double long_sum = prices_[0];
        double short_sum = prices_[0];
        double long_alpha = 2.0 / (long_length_ + 1.0);
        double short_alpha = 2.0 / (short_length_ + 1.0);
        double signal_alpha = 2.0 / (signal_length_ + 1.0);

        for (size_t icase = 1; icase < prices_.size(); ++icase)
        {
            long_sum = long_alpha * prices_[icase] + (1.0 - long_alpha) * long_sum;
            short_sum = short_alpha * prices_[icase] + (1.0 - short_alpha) * short_sum;

            double diff = 0.5 * (long_length_ - 1.0) - 0.5 * (short_length_ - 1.0);
            double denom = std::sqrt(std::abs(diff));
            denom *= ATR(icase, lookback_period_); 

            macd_line[icase] = (short_sum - long_sum) / (denom + 1.e-15); // ATR normalises MACD to account for volatility
            macd_line[icase] = 100.0 * normal_cdf(1.0 + macd_line[icase]) - 50.0;

            //std::cout << "Price[" << icase << "]: " << prices_[icase] << ", Short EMA: " << short_sum 
                      //<< ", Long EMA: " << long_sum << ", MACD: " << macd_line[icase] << "\n";
        }

        // Calculate the signal line as an EMA of the MACD line
        signal_line[0] = macd_line[0];
        for (size_t icase = 1; icase < macd_line.size(); ++icase)
        {
            signal_line[icase] = signal_alpha * macd_line[icase] + (1.0 - signal_alpha) * signal_line[icase - 1];
        }

        if (n_to_smooth_ > 1)
        {
            double alpha = 2.0 / (n_to_smooth_ + 1.0);
            double smoothed = macd_line[0];
            for (size_t icase = 1; icase < macd_line.size(); ++icase)
            {
                smoothed = alpha * macd_line[icase] + (1.0 - alpha) * smoothed;
                macd_line[icase] -= smoothed;
            }
        }

        return {macd_line, signal_line};
    }


    double ATR(size_t end, size_t lookback)
    {
        if (end < lookback) { 
            lookback = end + 1; // Adjust lookback if not enough values
        }
    
        double atr = 0.0;
        size_t start = end - lookback + 1;

        for (size_t i = start; i <= end; ++i)
        {
            double high_low = high_window_[i - start] - low_window_[i - start];
            double high_close = std::abs(high_window_[i - start] - closes_[i - 1]);
            double low_close = std::abs(low_window_[i - start] - closes_[i - 1]);
            atr += std::max({high_low, high_close, low_close});
            //std::cout << "ATR: " << atr << "\n";
        }

        return atr / lookback;
    }

    double normal_cdf(double value)
    {  
        return 0.5 * std::erfc(-value / std::sqrt(2.0));
    } 

    void placeOrder(Order::Side side)
    {
        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, side, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }

        int quantity = 100;
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