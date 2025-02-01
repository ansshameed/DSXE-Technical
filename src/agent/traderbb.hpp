#ifndef TRADER_BOLLINGER_BANDS_HPP
#define TRADER_BOLLINGER_BANDS_HPP

#include <vector>
#include <numeric>
#include <algorithm>
#include "traderagent.hpp"

class TraderBollingerBands : public TraderAgent
{
public:

    TraderBollingerBands(NetworkEntity *network_entity, TraderConfigPtr config, int lookback_period, double std_dev_multiplier)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      lookback_period_{lookback_period},
      std_dev_multiplier_{std_dev_multiplier},
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
        std::cout << "Last price traded: " << msg->data->last_price_traded << "\n";

        // Collect closing prices from market data
        closing_prices_.push_back(msg->data->last_price_traded); // Add last price traded to closing prices vector 

        if (closing_prices_.size() > lookback_period_) // If closing price size exceeds lookback period, remove oldest closing price (for fixed-size window)
        {
            closing_prices_.erase(closing_prices_.begin());
        }

        // Calculate Bollinger Bands
        if (closing_prices_.size() >= lookback_period_) // If enough data points to calculate Bollinger Bands
        {
            double sma = calculateSMA(closing_prices_); // Calculate Simple Moving Average
            double std_dev = calculateStandardDeviation(closing_prices_, sma); // Calculate Standard Deviation
            double upper_band = sma + (std_dev_multiplier_ * std_dev); // Calculate Upper Band
            double lower_band = sma - (std_dev_multiplier_ * std_dev); // Calculate Lower Band

            std::cout << "SMA: " << sma << ", Upper Band: " << upper_band << ", Lower Band: " << lower_band << "\n";

            // Implement trading logic based on Bollinger Bands
            if (trader_side_ == Order::Side::BID && msg->data->last_price_traded < lower_band) // If trader is a buyer and last price traded is below lower band
            {
                // Buy signal
                placeOrder(Order::Side::BID, lower_band); // Place BID order with lower band price
            }
            else if (trader_side_ == Order::Side::ASK && msg->data->last_price_traded > upper_band) // If trader is a seller and last price traded is above upper band
            {
                // Sell signal
                placeOrder(Order::Side::ASK, upper_band); // Place ASK order with upper band price
            }
        }
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        // Order added to order book
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

    double calculateSMA(const std::vector<double>& prices)
    {
        double sum = std::accumulate(prices.begin(), prices.end(), 0.0); // Sum all elements in prices (begin and end define range to sum over). 0.0 = initial value of sum
        return sum / prices.size(); // Calculate Simple Moving Average; sum divided by number of elements in prices
    }

    double calculateStandardDeviation(const std::vector<double>& prices, double sma)
    {
        double sum = 0.0; // Initialise sum to 0
        for (double price : prices) // For each price in prices
        {
            sum += (price - sma) * (price - sma); // Add square of difference between price and SMA to sum (sum of squared differences)
        }
        return std::sqrt(sum / (prices.size() - 1)); // Calculate Standard Deviation; square root of sum divided by number of elements in prices minus 1; sample variance
    }

    void placeOrder(Order::Side side, double band_price)
    {
        if (cancelling_ && last_accepted_order_id_.has_value()) // If cancelling is enabled and there is an accepted order (checks if there is previously placed order that has been accepted )
        {
            cancelOrder(exchange_, side, ticker_, last_accepted_order_id_.value()); // Cancel previously placed order
            last_accepted_order_id_ = std::nullopt; // Reset last accepted order ID
        }

        int quantity = getRandomOrderSize(); // Get random order size
        
        // Fix the price calculation: use a small adjustment instead of multiplying
        double price_adjustment = 0.002 * band_price; // 0.2% of price as slippage. Slippage = difference between expected price and actual price
        double price = (side == Order::Side::BID) ? (band_price + price_adjustment) // If BID then increase price slightly, else decrease price slightly (ASK) 
                                                : (band_price - price_adjustment);

        placeLimitOrder(exchange_, side, ticker_, quantity, price, limit_price_);

        std::cout << ">> " << (side == Order::Side::BID ? "BID" : "ASK") 
                << " " << quantity << " @ " << price << "\n";
    }


    double getQuotePrice(Order::Side side)
    {
        double price = round(limit_price_ * (1 + profit_margin_));
        if (side == Order::Side::BID)
        {
            price = std::min(limit_price_, price);
        }
        else
        {
            price = std::max(limit_price_, price);
        }
        return price;
    }

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    int lookback_period_;
    double std_dev_multiplier_;
    bool cancelling_;
    unsigned int trade_interval_ms_;
    std::vector<double> closing_prices_;

    std::optional<int> last_accepted_order_id_ = std::nullopt;

    std::mt19937 random_generator_;

    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;

    double profit_margin_; 
    constexpr static double REL_JITTER = 0.25;
};

#endif