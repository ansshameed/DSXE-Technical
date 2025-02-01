#ifndef TRADER_BB_RSI_HPP
#define TRADER_BB_RSI_HPP

#include <vector>
#include <numeric>
#include <algorithm>
#include "traderagent.hpp"

class TraderBBRSI : public TraderAgent
{
public:

    TraderBBRSI(NetworkEntity *network_entity, TraderConfigPtr config, int lookback_bb, int lookback_rsi, double std_dev_multiplier)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      lookback_bb_{lookback_bb},
      lookback_rsi_{lookback_rsi},
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

        double last_price = msg->data->last_price_traded;

        // Maintain separate buffers for RSI and Bollinger Bands
        rsi_prices_.push_back(last_price);
        bb_prices_.push_back(last_price);

        if (rsi_prices_.size() > lookback_rsi_) {
            rsi_prices_.erase(rsi_prices_.begin());
        }

        if (bb_prices_.size() > lookback_bb_) {
            bb_prices_.erase(bb_prices_.begin());
        }

        // Calculate RSI
        double rsi = 50.0;
        if (rsi_prices_.size() >= lookback_rsi_) {
            rsi = calculateRSI(rsi_prices_);
            std::cout << "RSI: " << rsi << "\n";
        }

        // Calculate Bollinger Bands
        if (bb_prices_.size() >= lookback_bb_) {
            double sma = calculateSMA(bb_prices_);
            double std_dev = calculateStandardDeviation(bb_prices_, sma);
            double upper_band = sma + (std_dev_multiplier_ * std_dev);
            double lower_band = sma - (std_dev_multiplier_ * std_dev);

            std::cout << "SMA: " << sma << ", Upper Band: " << upper_band << ", Lower Band: " << lower_band << "\n";

            // Trading logic based on RSI and Bollinger Bands
            if (trader_side_ == Order::Side::BID && last_price < lower_band && rsi < 30)
            {
                std::cout << "Price below lower Bollinger Band and RSI < 30, placing BID order\n";
                placeOrder(Order::Side::BID, lower_band);
            }
            else if (trader_side_ == Order::Side::ASK && last_price > upper_band && rsi > 70)
            {
                std::cout << "Price above upper Bollinger Band and RSI > 70, placing ASK order\n";
                placeOrder(Order::Side::ASK, upper_band);
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

    double calculateSMA(const std::vector<double>& prices)
    {
        double sum = std::accumulate(prices.begin(), prices.end(), 0.0);
        return sum / prices.size();
    }

    double calculateStandardDeviation(const std::vector<double>& prices, double sma)
    {
        double sum = 0.0;
        for (double price : prices)
        {
            sum += (price - sma) * (price - sma);
        }
        return std::sqrt(sum / (prices.size() - 1));
    }

    double calculateRSI(const std::vector<double>& prices)
    {
        if (prices.size() < lookback_rsi_) {
            return 50.0; // Neutral RSI
        }

        double upsum = 0.0, dnsm = 0.0;
        size_t initial_calculation_period = std::min(static_cast<size_t>(lookback_rsi_), prices.size());

        for (size_t i = 1; i < initial_calculation_period; ++i)
        {
            double diff = prices[i] - prices[i - 1];
            if (diff > 0.0)
                upsum += diff;
            else
                dnsm += -diff;
        }
        upsum /= (lookback_rsi_ - 1);
        dnsm /= (lookback_rsi_ - 1);

        for (size_t i = initial_calculation_period; i < prices.size(); ++i)
        {
            double diff = prices[i] - prices[i - 1];
            if (diff > 0.0)
            {
                upsum = ((lookback_rsi_ - 1) * upsum + diff) / lookback_rsi_;
                dnsm *= (lookback_rsi_ - 1.0) / lookback_rsi_;
            }
            else
            {
                dnsm = ((lookback_rsi_ - 1) * dnsm - diff) / lookback_rsi_;
                upsum *= (lookback_rsi_ - 1.0) / lookback_rsi_;
            }
        }

        if (upsum + dnsm < 1e-6) {
            return 50.0;
        }

        double rsi = 100.0 * (upsum / (upsum + dnsm));
        return rsi;
    }

    void placeOrder(Order::Side side, double band_price)
    {
        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, side, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }

        int quantity = getRandomOrderSize();

        double price_adjustment = 0.002 * band_price;
        double price = (side == Order::Side::BID) ? (band_price + price_adjustment)
                                                  : (band_price - price_adjustment);

        placeLimitOrder(exchange_, side, ticker_, quantity, price, limit_price_);

        std::cout << ">> " << (side == Order::Side::BID ? "BID" : "ASK") 
                  << " " << quantity << " @ " << price << "\n";
    }

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    int lookback_bb_;
    int lookback_rsi_;
    double std_dev_multiplier_;
    bool cancelling_;
    unsigned int trade_interval_ms_;
    std::vector<double> closing_prices_;

    std::optional<int> last_accepted_order_id_ = std::nullopt;

    std::mt19937 random_generator_;

    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;

    std::vector<double> rsi_prices_;  // Stores prices for RSI calculation
    std::vector<double> bb_prices_;   // Stores prices for Bollinger Bands calculation

    constexpr static double REL_JITTER = 0.25;
};

#endif