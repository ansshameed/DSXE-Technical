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
        // Mark as legacy agent
        is_legacy_trader_ = false;

        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);
    }

    std::string getAgentName() const override { return "rsibb"; }

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
        if (msg->order->status == Order::Status::NEW) // If order status is new (order added to order book)
        {
            last_accepted_order_id_ = msg->order->id; // Store the order ID
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
                std::cout << "[RSIBB] Enqueued CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
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
                    // If we have enough data, use RSI+BB strategy with customer order parameters
                    if (rsi_prices_.size() >= lookback_rsi_ && bb_prices_.size() >= lookback_bb_) 
                    { 
                        double rsi = calculateRSI(rsi_prices_); 
                        double sma = calculateSMA(bb_prices_);
                        double std_dev = calculateStandardDeviation(bb_prices_, sma);
                        double upper_band = sma + (std_dev_multiplier_ * std_dev);
                        double lower_band = sma - (std_dev_multiplier_ * std_dev);
                        placeOrder(rsi, upper_band, lower_band);
                    }
                    // Else directly process customer orders to bootstrap market data
                    else
                    {
                        processCustomerOrder();
                    }
                }
                // Normal RSI+BB strategy for regular trading
                else if (rsi_prices_.size() >= lookback_rsi_ && bb_prices_.size() >= lookback_bb_) 
                { 
                    double rsi = calculateRSI(rsi_prices_); 
                    double sma = calculateSMA(bb_prices_);
                    double std_dev = calculateStandardDeviation(bb_prices_, sma);
                    double upper_band = sma + (std_dev_multiplier_ * std_dev);
                    double lower_band = sma - (std_dev_multiplier_ * std_dev);
                    placeOrder(rsi, upper_band, lower_band);
                }
                else 
                { 
                    std::cout << "Not enough data for RSI+BB calculation.\n";
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
            std::cout << "[ERROR] Invalid market data received.\n";
            return;
        }

        double price = msg->data->last_price_traded;
        if (price <= 0) 
        { 
            std::cout << "[ERROR] Invalid market price received: " << price << "\n";
            return;
        }

        rsi_prices_.push_back(price);
        bb_prices_.push_back(price);

        if (rsi_prices_.size() > lookback_rsi_)
        {
            rsi_prices_.erase(rsi_prices_.begin());
        }

        if (bb_prices_.size() > lookback_bb_)
        {
            bb_prices_.erase(bb_prices_.begin());
        }

        last_market_data_ = msg->data;
    } 

    void placeOrder(double rsi, double upper_band, double lower_band)
    {

        if (!last_market_data_.has_value())
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
        double last_price = last_market_data_.value()->last_price_traded;
        double best_bid = last_market_data_.value()->best_bid;
        double best_ask = last_market_data_.value()->best_ask;
        bool should_place_order = false;

        // Relaxed conditions: trade if EITHER RSI or Bollinger Bands signal or make it restrictive with ANS
        if (trader_side_ == Order::Side::BID) 
        {
            // Buy conditions: RSI < 30 OR price below lower band
            if (rsi < 30 && last_price < lower_band) 
            {
                should_place_order = true;
            }
        }
        else if (trader_side_ == Order::Side::ASK) 
        {
            // Sell conditions: RSI > 70 AND price above upper band
            if (rsi > 70 && last_price > upper_band) 
            {
                should_place_order = true;
            }
        }
        
        if (should_place_order) 
        {
            double price = getQuotePrice(rsi, upper_band, lower_band, best_bid, best_ask, trader_side_);
            placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
            std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << " | RSI: " << rsi << " | BB Range: [" << lower_band << ", " << upper_band << "]\n";
        }
        else
        {
            std::cout << "Trade conditions NOT met. No order placed.\n" << "RSI: " << rsi << " | Last Price: " << last_price << " | BB Range: [" << lower_band << ", " << upper_band << "]\n";
        }
    }

    double getQuotePrice(double rsi, double upper_band, double lower_band, double best_bid, double best_ask, Order::Side trader_side_)
    {
        double price;
        if (trader_side_ == Order::Side::BID)
        {
            // Prioritise lifting the ask
            price = best_ask; 
            return std::min(price, limit_price_);
        }
        else
        {
            // Prioritise hitting the bid
            price = best_bid; 
            return std::max(price, limit_price_);
        }
    }

    double calculateSMA(const std::vector<double>& prices)
    {   
        if (prices.empty()) return 0.0; // Avoid divison by 0
        double sum = std::accumulate(prices.begin(), prices.end(), 0.0); 
        return sum / prices.size(); // Simple Moving Avergge 
    }

    double calculateStandardDeviation(const std::vector<double>& prices, double sma) 
    {
        double sum = 0.0;
        for (double price : prices) // Iterate through prices 
        {
            sum += (price - sma) * (price - sma); // Add square of difference between price and SMA to sum (sum of squared differences)
        }
        return std::sqrt(sum / (prices.size() - 1)); // Standard deviation
    }

    double calculateRSI(const std::vector<double>& prices) // Calculate RSI based on closing prices
    {
        if (prices.size() < lookback_rsi_) { // If prices size less than lookback period, return neutral RSI
            return 50.0; 
        }

        double upsum = 0.0, dnsm = 0.0; // Initialise upsum and dnsm to 0 (gain and loss)
        size_t initial_calculation_period = std::min(static_cast<size_t>(lookback_rsi_), prices.size()); // Initial calculation period (minimum of lookback period and prices size)

        for (size_t i = 1; i < initial_calculation_period; ++i) // Loop through lookback period 
        {
            double diff = prices[i] - prices[i - 1]; // Price difference for each consecutive price pair
            if (diff > 0.0) 
                upsum += diff; 
            else
                dnsm += -diff; 
        }
        // Average of upward and downward price movements (over lookback period)
        upsum /= (lookback_rsi_ - 1); 
        dnsm /= (lookback_rsi_ - 1); 

        //RSI continuously updated using exponential smoothing (historical trend via upsum and dnsm and the new prices differences)
        for (size_t i = initial_calculation_period; i < prices.size(); ++i) 
        {
            double diff = prices[i] - prices[i - 1]; // Price difference for each consecutive price pair
            if (diff > 0.0) // If diff > 0.0 its upward price movement
            {
                upsum = ((lookback_rsi_ - 1) * upsum + diff) / lookback_rsi_; // Exponential smoothing
                dnsm *= (lookback_rsi_ - 1.0) / lookback_rsi_; 
            }
            else
            {
                dnsm = ((lookback_rsi_ - 1) * dnsm - diff) / lookback_rsi_; // Exponential smoothing
                upsum *= (lookback_rsi_ - 1.0) / lookback_rsi_; 
            }
        }

        if (upsum + dnsm < 1e-6) { // Prevents divison by 0 when calculating RSI by returning neutral value (50) if sum of upward and downward price movement is extremely small (less than 1e-6)
            return 50.0; // Neutral RSI
        }

        return 100.0 * (upsum / (upsum + dnsm)); // Calculate RSI; normalises ratio of avg. gains (upsum) to total movement (upsum + dnsm) to 0-100 scale. RSI < 30: oversold, RSI > 70: overbought
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
    int lookback_bb_;
    int lookback_rsi_;
    double std_dev_multiplier_;
    bool cancelling_;
    unsigned int trade_interval_ms_;
    std::vector<double> rsi_prices_;
    std::vector<double> bb_prices_;
    std::optional<int> last_accepted_order_id_ = std::nullopt;
    std::mt19937 random_generator_;
    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;
    unsigned long next_trade_timestamp_;
    std::optional<MarketDataPtr> last_market_data_;
    constexpr static double REL_JITTER = 0.25;
    constexpr static unsigned long MS_TO_NS = 1000000;
    std::stack<CustomerOrderMessagePtr> customer_orders_;

};

#endif