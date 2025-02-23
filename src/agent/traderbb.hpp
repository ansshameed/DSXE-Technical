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

    std::string getAgentName() const override { return "Bollinger Bands"; }

    void terminate() override
    {
        if (trading_thread_ != nullptr)
        {
            trading_thread_->join();
            delete trading_thread_;
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
        // Order added to order book
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
                    if (!closing_prices_.empty()) 
                    {
                        double sma = calculateSMA(closing_prices_);
                        double std_dev = calculateStandardDeviation(closing_prices_, sma);
                        double upper_band = sma + (std_dev_multiplier_ * std_dev);
                        double lower_band = sma - (std_dev_multiplier_ * std_dev);

                        double last_price = closing_prices_.back();
                        std::cout << "Calculated Bollinger Bands: Upper: " << upper_band << " | Lower: " << lower_band << " | Last Price: " << last_price << "\n";

                        placeOrder(last_price, upper_band, lower_band);
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

    void placeOrder(double last_price, double upper_band, double lower_band)
    {
        if (cancelling_ && last_accepted_order_id_.has_value()) // If cancelling is enabled and there is an accepted order (checks if there is previously placed order that has been accepted )
        {
            cancelOrder(exchange_, trader_side_, ticker_, last_accepted_order_id_.value()); // Cancel previously placed order
            last_accepted_order_id_ = std::nullopt; // Reset last accepted order ID
        }

        if (!last_market_data_.has_value()) {
            std::cout << "No market data available, skipping order placement.\n";
            return;
        }

        //int quantity = getRandomOrderSize(); // Get random order size
        int quantity = 100; 
        
        double best_bid = last_market_data_.value()->best_bid;
        double best_ask = last_market_data_.value()->best_ask;
        double price = getQuotePrice(last_price, upper_band, lower_band, best_bid, best_ask); // Get quote price based on side


        placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);

        std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << " | Last Price: " << last_price << " | Upper Band: " << upper_band << " | Lower Band: " << lower_band << "\n";
    } 

    double getQuotePrice(double last_price, double upper_band, double lower_band, double best_bid, double best_ask)
    {   
        double price; 
        double slippage = std::round(getRandom(-1, 1)); 

        if (trader_side_ == Order::Side::BID)
        {
            if (last_price < lower_band)
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
            if (last_price > upper_band)
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

    void reactToMarket(MarketDataMessagePtr msg)
    {
        std::cout << "Last price traded: " << msg->data->last_price_traded << "\n";

        if (msg->data->last_price_traded <= 0)
        {
            std::cout << "[ERROR] Invalid market price received: " << msg->data->last_price_traded << "\n";
            return;
        }

        closing_prices_.push_back(msg->data->last_price_traded);

        if (closing_prices_.size() > lookback_period_)
        {
            closing_prices_.erase(closing_prices_.begin());
        }

        last_market_data_ = msg->data;
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

    unsigned long long timeNow()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
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
    unsigned long next_trade_timestamp_;
    constexpr static double REL_JITTER = 0.25;
    constexpr static unsigned long MS_TO_NS = 1000000;
    std::optional<MarketDataPtr> last_market_data_;
};

#endif