#ifndef TRADER_VWAP_HPP
#define TRADER_VWAP_HPP

#include <vector>
#include <numeric>
#include <algorithm>
#include "traderagent.hpp"
#include "../message/profitmessage.hpp"

class TraderVWAP : public TraderAgent
{
public:
    TraderVWAP(NetworkEntity *network_entity, TraderConfigPtr config, int lookback)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      lookback_{lookback},
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

    std::string getAgentName() const override { return "VWAP"; }

    /** Gracefully terminates the trader, freeing all memory. */
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
            std::cout << "Trade Executed! Price: " << msg->trade->price
                      << " | Quantity: " << msg->trade->quantity
                      << " | Order ID: " << msg->order->id << std::endl;
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
                    std::vector<std::pair<double, double>> data_copy;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        data_copy = price_volume_data_;
                    }
                    if (!data_copy.empty())
                    {
                        double rolling_vwap = calculateVWAP(data_copy);
                        double last_price = data_copy.back().first;
                        std::cout << "Calculated Rolling VWAP: " << rolling_vwap << " (Last price: " << last_price << ")\n";
                        placeOrder(rolling_vwap, last_price);
                    }
                    next_trade_timestamp_ = timeNow() + (trade_interval_ms_ * MS_TO_NS);
                }
                sleep();
                lock.lock();
            }
            lock.unlock();
            std::cout << "Finished actively trading.\n";
        });
    }

    void placeOrder(double vwap_price, double last_price)
    {
        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, trader_side_, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }

        int quantity = 100; 

        if (!last_market_data_.has_value()) {
            std::cout << "No market data available, skipping order placement.\n";
            return;
        }

        double best_bid = last_market_data_.value()->best_bid;
        double best_ask = last_market_data_.value()->best_ask;
        double rounded_vwap = std::round(vwap_price);

        if (!last_market_data_.has_value() || best_bid <= 0 || best_ask <= 0) {
            std::cout << "No valid bid/ask data, skipping order placement.\n";
            return;
        }

        double price = getQuotePrice(rounded_vwap, best_bid, best_ask, trader_side_);

        placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
        std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") 
                << " " << quantity << " @ " << price 
                << " (VWAP: " << vwap_price << " | Best Bid: " << best_bid 
                << " | Best Ask: " << best_ask << ")\n";
    }

    double getQuotePrice(double rounded_vwap, double best_bid, double best_ask, Order::Side trader_side_)
    {
        double price; 
        double slippage = getRandom(-1, 1); // Small variation in price

        if (trader_side_ == Order::Side::BID) // Buyer logic
        {
            if (rounded_vwap > best_ask) {  // Bullish
                price = best_bid + 1 + slippage;
            }
            else { // Bearish
                price = best_bid + slippage; // No aggressive adjustment
            }

            return std::min(price, limit_price_);
        }

        else // Seller logic
        {
            if (rounded_vwap < best_bid) { // Bearish
                price = best_ask - 1 + slippage; 
            } 
            else { // Bullish
                price = best_ask + slippage; // No aggressive adjustment
            }

            return std::max(price, limit_price_);
        }
    }

    void reactToMarket(MarketDataMessagePtr msg)
    { 
        double closing_price = msg->data->last_price_traded; // Closing price is the last price traded
        double volume = msg->data->volume_per_tick; // Volume is the last quantity traded

        {
            std::unique_lock<std::mutex> lock(mutex_);
            price_volume_data_.emplace_back(closing_price, volume);
            if (price_volume_data_.size() > lookback_)
            {
                price_volume_data_.erase(price_volume_data_.begin());
            }
        }

        last_market_data_ = msg->data;
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
        double price_volume_sum = 0.0; // Sum of price * volume
        double volume_sum = 0.0; // Sum of volume

        for (const auto& [price, volume] : data) // For each price-volume pair in the data
        {
            price_volume_sum += price * volume; // Add the price * volume to the sum
            volume_sum += volume; // Add the volume to the sum
        }

        return volume_sum > 0 ? price_volume_sum / volume_sum : 0.0; // Calculate VWAP as price-volume sum divided by volume sum
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
    bool cancelling_;
    unsigned int trade_interval_ms_;
    int lookback_;
    std::vector<std::pair<double, double>> price_volume_data_;
    std::optional<int> last_accepted_order_id_ = std::nullopt;
    std::mt19937 random_generator_;
    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;
    unsigned long next_trade_timestamp_;
    constexpr static double REL_JITTER = 0.25;
    constexpr static unsigned long MS_TO_NS = 1000000;
    std::optional<MarketDataPtr> last_market_data_;
};

#endif