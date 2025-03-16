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

        // Mark as legacy agent
        is_legacy_trader_ = false;

        // Connect to exchange and subscribe to market data
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Start with a delayed execution
        addDelayedStart(config->delay);
    }

    std::string getAgentName() const override { return "obvvwap"; }

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
        //sendProfitToExchange();
        std::cout << "Trading window ended.\n";
        // Delay shutdown to allow profit message to be sent completely.
        lock.unlock();
        is_trading_ = false;
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
                std::cout << "[OBV + VWAP] Enqueued CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
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
                
                // Process any pending customer orders
                if (!customer_orders_.empty())
                {
                    // If we have enough data, use OBV+VWAP strategy with customer order parameters
                    if (price_volume_data_.size() >= lookback_vwap_ && close_prices_.size() >= lookback_obv_)
                    {
                        double rolling_vwap = calculateVWAP(price_volume_data_);
                        std::vector<double> delta_obv_values = calculateOBVDelta();
                        placeOrder(delta_obv_values.back(), rolling_vwap);
                    }
                    // Else directly process customer orders to bootstrap market data
                    else
                    {
                        processCustomerOrder();
                    }
                }
                // Normal OBV+VWAP strategy for regular trading
                else if (price_volume_data_.size() >= lookback_vwap_ && close_prices_.size() >= lookback_obv_)
                {
                    double rolling_vwap = calculateVWAP(price_volume_data_);
                    std::vector<double> delta_obv_values = calculateOBVDelta();
                    placeOrder(delta_obv_values.back(), rolling_vwap);
                }
                else
                {
                    std::cout << "Not enough market data for OBV+VWAP calculation.\n";
                }
                
                sleep();
                lock.lock();
            }
            lock.unlock();
            std::cout << "Finished actively trading.\n";
        });
    }

    void processCustomerOrder()
    {
        auto cust_order = customer_orders_.top();
        customer_orders_.pop();

        Order::Side order_side = cust_order->side;
        double order_price = cust_order->price;
        std::uniform_int_distribution<int> dist(10, 50);
        int quantity = dist(random_generator_);
        
        // Place order using customer injected order data
        placeLimitOrder(exchange_, order_side, ticker_, quantity, order_price, order_price);
        std::cout << ">> Customer Order (Bootstrap): " << (order_side == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << order_price << "\n";
    }


    void reactToMarket(MarketDataMessagePtr msg)
    {
        if (!msg || !msg->data)
        {
            std::cerr << "[ERROR] Invalid market data received.\n";
            return;
        }

        double price = msg->data->last_price_traded;
        double volume = msg->data->last_quantity_traded;

        price_volume_data_.emplace_back(price, volume);
        close_prices_.push_back(price);
        volumes_.push_back(volume);

        if (price_volume_data_.size() > lookback_vwap_)
            price_volume_data_.erase(price_volume_data_.begin());

        if (close_prices_.size() > lookback_obv_)
        {
            close_prices_.erase(close_prices_.begin());
            volumes_.erase(volumes_.begin());
        }

        last_market_data_ = msg->data;
    }

    void placeOrder(double delta_obv, double rolling_vwap)
    {
        if (!last_market_data_.has_value() || last_market_data_.value()->best_bid <= 0 || last_market_data_.value()->best_ask <= 0)
        {
            std::cout << "No valid bid/ask data, skipping order placement.\n";
            return;
        }

        if (cancelling_ && last_accepted_order_id_.has_value()) //Check if cancelling is enabled and last order was accepted
        {
            cancelOrder(exchange_, trader_side_, ticker_, last_accepted_order_id_.value()); //Cancel last accepted order
            last_accepted_order_id_ = std::nullopt; //Clears last accepted order to null
        }

        if (!customer_orders_.empty()) 
        {   
            auto cust_order = customer_orders_.top(); // Get next customer order
            customer_orders_.pop();
            limit_price_ = cust_order->price;
            trader_side_ = cust_order->side;
        } 

        std::uniform_int_distribution<int> dist(10, 50);  
        int quantity = dist(random_generator_);     
        double best_bid = last_market_data_.value()->best_bid; //Get best bid price from market data
        double best_ask = last_market_data_.value()->best_ask; //Get best ask price from market data
        double last_price = last_market_data_.value()->last_price_traded;
        bool should_place_order = false;
    
        // Relaxed conditions: trade if EITHER OBV Delta OR VWAP signal
        if (trader_side_ == Order::Side::BID) 
        {
            // Buy conditions: delta_obv > threshold AND last price below VWAP (OR for flexibility in trades).
            if (delta_obv > (0.5 * threshold_) || last_price < rolling_vwap) 
            {
                should_place_order = true;
            }
        }
        else if (trader_side_ == Order::Side::ASK) 
        {
            // Sell conditions: delta_obv < -threshold OR last price above VWAP
            if (delta_obv < (-0.5 * threshold_) || last_price > rolling_vwap) 
            {
                should_place_order = true;
            }
        }

        if (should_place_order) 
        {
            double price = getQuotePrice(delta_obv, rolling_vwap, best_bid, best_ask, trader_side_);
            placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
            std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << " | OBV Delta: " << delta_obv << " | VWAP: " << rolling_vwap << "\n";
        }
        else
        {
            std::cout << "Trade conditions NOT met. No order placed.\n" << "OBV Delta: " << delta_obv << " | Last Price: " << last_price << " | VWAP: " << rolling_vwap << "\n";
        }
    } 
    

    double getQuotePrice(double delta_obv, double rolling_vwap, double best_bid, double best_ask, Order::Side trader_side_)
    {
        double price;

        if (trader_side_ == Order::Side::BID)
        {
            // Lifting the ask
            price = best_ask; 
            return std::min(price, limit_price_);
        }
        else
        {
            // Hitting the bid
            price = best_bid; 
            return std::max(price, limit_price_);
        }
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

    std::vector<double> calculateOBVDelta()
    {
        size_t n = close_prices_.size();
        if (n < lookback_obv_ || volumes_.size() < lookback_obv_) 
        {
            return {};
        }

        std::vector<double> output(n, 0.0);
        size_t front_bad = lookback_obv_;

        for (size_t first_volume = 0; first_volume < n; first_volume++) 
        {
            if (volumes_[first_volume] > 0) 
            {
                break;
            }
            front_bad = std::min(front_bad, n - 1);
        }

        for (size_t i = 0; i < front_bad; i++) 
        {
            output[i] = 0.0;
        }

        for (size_t icase = front_bad; icase < n; icase++) 
        {
            double signed_volume = 0.0;
            double total_volume = 0.0;

            for (size_t i = 1; i < lookback_obv_ && (icase - i) > 0; i++) 
            {
                if (close_prices_[icase - i] > close_prices_[icase - i - 1]) 
                {
                    signed_volume += volumes_[icase - i];
                } 
                else if (close_prices_[icase - i] < close_prices_[icase - i - 1]) 
                {
                    signed_volume -= volumes_[icase - i];
                }
                total_volume += volumes_[icase - i];
            }

            if (total_volume <= 0.0) 
            {
                output[icase] = 0.0;
                continue;
            }

            double value = signed_volume / total_volume;
            double normalized_value = 100.0 * std::erfc(-0.6 * value * sqrt(static_cast<double>(lookback_obv_))) - 50.0;
            output[icase] = normalized_value;
        }

        if (n < front_bad + delta_length_) 
        {
            std::cerr << "ERROR: Not enough data for delta calculation!\n";
            return output;
        }

        for (int icase = static_cast<int>(n) - 1; icase >= static_cast<int>(front_bad + delta_length_); icase--) 
        {
            output[icase] -= output[icase - delta_length_];
        }

        return output;
    }
    void sleep()
    {
        std::uniform_real_distribution<> dist(-REL_JITTER, REL_JITTER);
        unsigned long jitter = dist(random_generator_);
        unsigned long sleep_time_ms = std::round(trade_interval_ms_ * (1.0 + jitter));
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
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
    constexpr static unsigned long MS_TO_NS = 1000000;
    std::optional<MarketDataPtr> last_market_data_;
    unsigned long next_trade_timestamp_;
    std::stack<CustomerOrderMessagePtr> customer_orders_;
};

#endif