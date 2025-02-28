#ifndef TRADER_VWAP_HPP
#define TRADER_VWAP_HPP

#include <vector>
#include <numeric>
#include <stack> 
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

    void handleBroadcastFrom(std::string_view sender, MessagePtr message) override
    {
        if (message->type == MessageType::CUSTOMER_ORDER) 
        {
            auto cust_msg = std::dynamic_pointer_cast<CustomerOrderMessage>(message);
            if (cust_msg) 
            { 
                std::lock_guard<std::mutex> lock(mutex_);
                customer_orders_.push(cust_msg); // Enqueue customer order
                std::cout << "[VWAP] Enqueued CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
            }
            return; // Just exit the function, don’t call base handler
        }

        // If it's not a customer order, call the base class handler
        TraderAgent::handleBroadcastFrom(sender, message);
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

                // Check if we have at least 'lookback_' entries
                if (price_volume_data_.size() >= lookback_)
                {
                    double rolling_vwap = calculateVWAP(price_volume_data_); 
                    double last_price  = price_volume_data_.back().first;
                    std::cout << "Calculated Rolling VWAP: " << rolling_vwap << " (Last price: " << last_price << ")\n";
                    placeOrder(rolling_vwap, last_price);
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

        if (!customer_orders_.empty()) 
        {   
            auto cust_order = customer_orders_.top(); // Get next customer order
            customer_orders_.pop();
            limit_price_ = cust_order->price;
            trader_side_ = cust_order->side;
        }

        double best_bid = last_market_data_.value()->best_bid;
        double best_ask = last_market_data_.value()->best_ask;
        double rounded_vwap = std::round(vwap_price);

        int quantity = 100; 
        double price = getQuotePrice(rounded_vwap, best_bid, best_ask, trader_side_);
        placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
        std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") 
                << " " << quantity << " @ " << price 
                << " (VWAP: " << rounded_vwap << " | Best Bid: " << best_bid 
                << " | Best Ask: " << best_ask << ")\n";
    }

    double getQuotePrice(double rounded_vwap, double best_bid, double best_ask, Order::Side trader_side_)
    {
        double price; 
        double slippage = std::round(getRandom(-1, 1)); // Small variation in price

        if (trader_side_ == Order::Side::BID) 
        { 
            if (rounded_vwap > best_ask) 
            { 
                return best_ask; 
            }
            else 
            { 
                return best_bid - 1; 
            }
        }

        else 
        { 
            if (rounded_vwap < best_bid) 
            { 
                return best_bid; 
            }
            else 
            { 
                return best_ask + 1; 
            }
        }
    }

    void reactToMarket(MarketDataMessagePtr msg)
    { 
        double closing_price = msg->data->last_price_traded; 
        double volume = msg->data->last_quantity_traded; 
        std::cout << "closing price: " << closing_price << " volume: " << volume << std::endl;

        price_volume_data_.emplace_back(closing_price, volume);
        if (price_volume_data_.size() > lookback_)
        {
            price_volume_data_.erase(price_volume_data_.begin());
        }

        last_market_data_ = msg->data;

        // Debugging: Print stored market data
        std::cout << "[DEBUG] Stored Market Data - Price: " << closing_price << ", Volume: " << volume << ", Buffer Size: " << price_volume_data_.size() << "\n";
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
    std::stack<CustomerOrderMessagePtr> customer_orders_;
};

#endif