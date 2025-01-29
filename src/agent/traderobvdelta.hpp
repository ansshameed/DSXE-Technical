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
        
        close_prices_.push_back(msg->data->last_price_traded);
        volumes_.push_back(msg->data->volume_per_tick); // CHANGE BECAUSE THIS IS VOLUME SINCE MARKET OPEN; NEED TRADE VOLUME PER TICK
        std::cout << "DEBUG: Current Volume: " << msg->data->volume_per_tick << "\n"; 

        if (close_prices_.size() >= lookback_length_)
        {
            auto delta_obv = calculateDeltaOBV(close_prices_, volumes_, lookback_length_, delta_length_);
            
            if (delta_obv.empty()) {
                std::cerr << "ERROR: delta_obv is empty!" << std::endl;
                return;
            }
            double latest_delta_obv = delta_obv.back();

            if (latest_delta_obv > threshold_ && trader_side_ == Order::Side::BID)
            {
                placeOrder(Order::Side::BID);
            }
            else if (latest_delta_obv < -threshold_ && trader_side_ == Order::Side::ASK)
            {
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
        std::uniform_real_distribution<> dist(-REL_JITTER, REL_JITTER);
        unsigned long jitter = dist(random_generator_);
        unsigned long sleep_time_ms = std::round(trade_interval_ms_ * (1.0 + jitter));
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
    }

    std::vector<double> calculateDeltaOBV(const std::vector<double>& close_prices, const std::vector<double>& volumes, int lookback_length, int delta_length)
    {   
        if (close_prices.size() < lookback_length || volumes.size() < lookback_length) {
            throw std::invalid_argument("Insufficient data for the given lookback length.");
        }

        size_t n = close_prices.size();
        std::vector<double> output(n, 0.0);
        size_t front_bad = lookback_length; // front_bad is the number of bad values at the front of the array (to skip initial elements in volume vector; ensures OBV calculation starts with valid data)

        for (size_t first_volume = 0; first_volume < n; first_volume++) {
            if (volumes[first_volume] > 0) {
                break;
            }
            front_bad++;
        }

        for (size_t i = 0; i < front_bad; i++) {
            output[i] = 0.0;
        }

        for (size_t icase = front_bad; icase < n; icase++) {
            double signed_volume = 0.0;
            double total_volume = 0.0;

            for (size_t i = 1; i < lookback_length && (icase - i) > 0 && (icase - i - 1) >= 0; i++) {  
                if (close_prices[icase - i] > close_prices[icase - i - 1]) {
                    signed_volume += volumes[icase - i];
                } else if (close_prices[icase - i] < close_prices[icase - i - 1]) {
                    signed_volume -= volumes[icase - i];
                }
                total_volume += volumes[icase - i];
            }

            if (total_volume <= 0.0) {
                output[icase] = 0.0;
                continue;
            }

            double value = signed_volume / total_volume;
            double normalized_value = 100.0 * std::erfc(-0.6 * value * sqrt(static_cast<double>(lookback_length))) - 50.0;
            output[icase] = normalized_value;
        }

        front_bad += delta_length;
        for (size_t icase = n - 1; icase >= front_bad && icase >= delta_length; icase--) {
            output[icase] -= output[icase - delta_length];
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

        int quantity = getRandomOrderSize(); 
        double price = getQuotePrice(side);
        placeLimitOrder(exchange_, side, ticker_, quantity, price, limit_price_);

        std::cout << ">> " << (side == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << "\n";
    }

    double getQuotePrice(Order::Side side)
    {
        double price = close_prices_.back();
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
};

#endif