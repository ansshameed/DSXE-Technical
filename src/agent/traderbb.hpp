#ifndef TRADER_BOLLINGER_BANDS_HPP
#define TRADER_BOLLINGER_BANDS_HPP

#include <vector>
#include <numeric>
#include <algorithm>
#include <stack> 
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
        // Mark as legacy agent
        is_legacy_trader_ = false;

        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);
    }

    std::string getAgentName() const override { return "bb"; }

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
        activelyTrade();
    }

    void onTradingEnd() override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        //sendProfitToExchange();
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

    void handleBroadcastFrom(std::string_view sender, MessagePtr message) override
    {
        if (message->type == MessageType::CUSTOMER_ORDER) 
        {
            auto cust_msg = std::dynamic_pointer_cast<CustomerOrderMessage>(message);
            if (cust_msg) 
            { 
                std::lock_guard<std::mutex> lock(mutex_);
                customer_orders_.push(cust_msg); // Enqueue customer order
                std::cout << "[Bollinger Bands] Enqueued CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
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
                {
                    // Process any pending customer orders to initiate orders
                    if (!customer_orders_.empty())
                    {
                        if (closing_prices_.size() >= lookback_period_)
                        {
                            double sma = calculateSMA(closing_prices_);
                            double std_dev = calculateStandardDeviation(closing_prices_, sma);
                            double upper_band = sma + (std_dev_multiplier_ * std_dev);
                            double lower_band = sma - (std_dev_multiplier_ * std_dev);
                            std::cout << "Calculated Bollinger Bands: Upper: " << upper_band << " | Lower: " << lower_band << "\n";
                            placeOrder(upper_band, lower_band);  
                        }
                        // Else directly process customer orders to bootstrap market data
                        else
                        {
                            processCustomerOrder();
                        }
                    }
                    // Apply strat
                    else if (closing_prices_.size() >= lookback_period_)
                    {
                        double sma = calculateSMA(closing_prices_);
                        double std_dev = calculateStandardDeviation(closing_prices_, sma);
                        double upper_band = sma + (std_dev_multiplier_ * std_dev);
                        double lower_band = sma - (std_dev_multiplier_ * std_dev);
                        std::cout << "Calculated Bollinger Bands: Upper: " << upper_band << " | Lower: " << lower_band << "\n";
                        placeOrder(upper_band, lower_band);
                    }
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
        std::cout << ">> Customer Order: " << (order_side == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << order_price << "\n";
    }

    void placeOrder(double upper_band, double lower_band)
    {
        // Cancel previous order if needed
        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, trader_side_, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }
        
        // Only proceed with Bollinger Bands strategy if we have market data
        if (!last_market_data_.has_value()) {
            std::cout << "No market data available, skipping order placement.\n";
            return;
        }

        if (!customer_orders_.empty()) 
        {   
            auto cust_order = customer_orders_.top(); // Get next customer order
            customer_orders_.pop();
            limit_price_ = cust_order->price;
            trader_side_ = cust_order->side; 
        }
        
        // Get market data
        double best_bid = last_market_data_.value()->best_bid;
        double best_ask = last_market_data_.value()->best_ask;
        double last_price = last_market_data_.value()->last_price_traded;
        double price = getQuotePrice(last_price, upper_band, lower_band, best_bid, best_ask);
        std::uniform_int_distribution<int> dist(10, 50);
        int quantity = dist(random_generator_);

        // Apply standard Bollinger Bands logic
        bool should_place_order = false;
        
        if (trader_side_ == Order::Side::BID && last_price < lower_band) 
        {
            should_place_order = true;
            std::cout << "Using Bollinger Bands" << "\n";
        }
        else if (trader_side_ == Order::Side::ASK && last_price > upper_band)
        {
            should_place_order = true;
            std::cout << "Using Bollinger Bands" << "\n";
        }
        
        if (should_place_order) 
        { 
            placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
            std::cout << ">> Bollinger Bands: " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price  << " | Upper Band: " << upper_band << " | Lower Band: " << lower_band << "\n";
        }
        else 
        { 
            std::cout << "Trade conditions NOT met. No order placed.\n";
        }
    }

    double getQuotePrice(double last_price, double upper_band, double lower_band, double best_bid, double best_ask)
    {   
       double candidate_price = 0.0; 
       
       if (trader_side_ == Order::Side::BID)
       {
          candidate_price = best_ask; // Immediately buy by lifting the ask 
          candidate_price = std::min(limit_price_, candidate_price); // Ensure price is not higher than the limit price
       } 
       else 
       { 
            candidate_price = best_bid; // Immediately sell by hitting the bid
            candidate_price = std::max(limit_price_, candidate_price); // Ensure price is not lower than the limit price
       }

       return candidate_price;
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
    unsigned int trade_interval_ms_ = 500; 
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
    std::stack<CustomerOrderMessagePtr> customer_orders_;

};

#endif