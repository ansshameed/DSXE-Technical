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
        std::cout << "Trading window ended.\n";
        is_trading_ = false;
        lock.unlock();
    }

    void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) override
    {
        std::cout << "Received market data from " << exchange << "\n";
        std::cout << "Last price traded: " << msg->data->last_price_traded << "\n";

        double closing_price = msg->data->last_price_traded;
        double volume = msg->data->volume_per_tick;

        // Store the most recent prices & volumes for VWAP and OBV Delta calculation
        price_volume_data_.emplace_back(closing_price, volume);
        close_prices_.push_back(closing_price);
        volumes_.push_back(volume);

        if (price_volume_data_.size() > lookback_vwap_)
        {
            price_volume_data_.erase(price_volume_data_.begin());
        }

        if (close_prices_.size() > lookback_obv_)
        {
            close_prices_.erase(close_prices_.begin());
            volumes_.erase(volumes_.begin());
        }

        // Calculate VWAP
        double rolling_vwap = calculateVWAP(price_volume_data_);
        std::cout << "Rolling VWAP: " << rolling_vwap << "\n";
        
        // Calculate OBV Delta
        auto delta_obv = calculateDeltaOBV(close_prices_, volumes_, lookback_obv_, delta_length_);
        if (delta_obv.empty())
        {
            std::cerr << "ERROR: delta_obv is empty!" << std::endl;
            return;
        }
        double latest_delta_obv = delta_obv.back();
        std::cout << "Latest Delta OBV: " << latest_delta_obv << "\n";

        // Implement trading logic based on VWAP and OBV Delta
        if (trader_side_ == Order::Side::BID && closing_price < rolling_vwap && latest_delta_obv > threshold_)
        {
            std::cout << "Price below VWAP and Delta OBV > threshold, placing BID order\n";
            placeOrder(Order::Side::BID, rolling_vwap);
        }
        else if (trader_side_ == Order::Side::ASK && closing_price > rolling_vwap && latest_delta_obv < -threshold_)
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

    double calculateVWAP(const std::vector<std::pair<double, double>>& data)
    {
        double price_volume_sum = 0.0;
        double volume_sum = 0.0;

        for (const auto& [price, volume] : data)
        {
            price_volume_sum += price * volume;
            volume_sum += volume;
        }

        return volume_sum > 0 ? price_volume_sum / volume_sum : 0.0;
    }

    std::vector<double> calculateDeltaOBV(const std::vector<double>& close_prices, 
                                          const std::vector<double>& volumes, 
                                          int lookback_length, 
                                          int delta_length)
    {   
        size_t n = close_prices.size();
        if (n < lookback_length || volumes.size() < lookback_length) {
            throw std::invalid_argument("Insufficient data for the given lookback length.");
        }

        std::vector<double> output(n, 0.0);
        size_t front_bad = lookback_length;

        for (size_t first_volume = 0; first_volume < n; first_volume++) {
            if (volumes[first_volume] > 0) {
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

            for (size_t i = 1; i < lookback_length && (icase - i) > 0; i++) {
                if (icase - i - 1 < 0) {
                    std::cerr << "ERROR: Out-of-bounds access prevented (icase - i - 1)" << std::endl;
                    break;
                }

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

        if (n < front_bad + delta_length) {
            std::cerr << "ERROR: Not enough data for delta calculation!" << std::endl;
            return output;
        }

        for (int icase = static_cast<int>(n) - 1; icase >= static_cast<int>(front_bad + delta_length); icase--) {
            if (icase - delta_length < 0) {
                std::cerr << "ERROR: Out-of-bounds access prevented (icase - delta_length)\n";
                break;
            }
            output[icase] -= output[icase - delta_length];
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

        double price_adjustment = 0.001 * vwap_price;
        double price = (side == Order::Side::BID) ? (vwap_price + price_adjustment)
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
};

#endif