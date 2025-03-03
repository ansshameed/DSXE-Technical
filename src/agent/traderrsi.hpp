#ifndef TRADER_RSI_HPP
#define TRADER_RSI_HPP

#include <vector>
#include <numeric>
#include <stack> 
#include <algorithm>
#include "traderagent.hpp"
#include "../message/profitmessage.hpp"

class TraderRSI : public TraderAgent
{
public:

    TraderRSI(NetworkEntity *network_entity, TraderConfigPtr config, int lookback, bool use_stoch_rsi, int stoch_lookback, int n_to_smooth)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      lookback_{lookback},
      cancelling_{config->cancelling},
      trade_interval_ms_{config->trade_interval}, 
      stoch_lookback_{stoch_lookback}, 
      n_to_smooth_{n_to_smooth}, // Smoothing factor
      random_generator_{std::random_device{}()},
      mutex_{}
    {   
        use_stoch_rsi_ = false; 

        // Mark as legacy agent
        is_legacy_trader_ = false;

        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);
    }

    std::string getAgentName() const override { return "RSI"; }

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

    //Handles execution report received from exchange when status update for order. Msg = param containing order details
    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        // Order added to order book
        if (msg->order->status == Order::Status::NEW) //Checks if order is new (NEW = order successfully added to LOB by exchange)
        {
            last_accepted_order_id_ = msg->order->id; //Tracks most recent order successfully added to LOB (can be used later for cancel/modifying)
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
                std::cout << "[RSI] Enqueued CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
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
        trading_thread_ = new std::thread([&, this](){
            std::unique_lock<std::mutex> lock(mutex_);
            while (is_trading_)
            {
                lock.unlock();
                if (closing_prices_.size() >= lookback_)
                { 
                    double rsi = calculateRSI(closing_prices_);
                    std::cout << "RSI: " << rsi << "\n";

                    double stoch_rsi = 50.0; // Neutral value.
                    if (use_stoch_rsi_)
                    { 
                        if (rsi_values_.size() >= stoch_lookback_)
                        { 
                            double stoch_rsi = calculateStochRSI(rsi_values_, stoch_lookback_, n_to_smooth_);
                            std::cout << "Stochastic RSI: " << stoch_rsi << "\n";
                        }
                        else
                        {
                            std::cout << "Not enough RSI values for StochRSI calculation.\n";
                            sleep();
                            lock.lock();
                            continue;
                        }
                    }
                    placeOrder(rsi, stoch_rsi); // Always call placeOrder 
                }
                else
                {
                    std::cout << "Not enough closing prices for RSI calculation.\n";
                }
                sleep();
                lock.lock();
            }
            lock.unlock();
            std::cout << "Finished actively trading.\n";
        });
    }

    void reactToMarket(MarketDataMessagePtr msg)
    {
        std::cout << "Last price traded: " << msg->data->last_price_traded << "\n";

        closing_prices_.push_back(msg->data->last_price_traded); // Add last price traded from market data to buffer

        if (closing_prices_.size() > lookback_) // If buffer exceeds lookback, remove the oldest price
        {
            closing_prices_.erase(closing_prices_.begin());
        }

        if (use_stoch_rsi_)
        { 
            if (closing_prices_.size() >= lookback_)
            {
                double rsi = calculateRSI(closing_prices_);
                rsi_values_.push_back(rsi);
                if (rsi_values_.size() > stoch_lookback_)
                {
                    rsi_values_.erase(rsi_values_.begin());
                }
            }
        }

        last_market_data_ = msg->data; // Store last market data
    } 

    void placeOrder(double rsi, double stoch_rsi) //Places limit order on exchange 
    {

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
            //trader_side_ = cust_order->side;
        }

        std::uniform_int_distribution<int> dist(10, 50);  // Generates integers between 0 and 100
        int quantity = dist(random_generator_);
        double best_bid = last_market_data_.value()->best_bid; //Get best bid price from market data
        double best_ask = last_market_data_.value()->best_ask; //Get best ask price from market data

        if (!last_market_data_.has_value()) {
            std::cout << "No valid bid/ask data, skipping order placement.\n";
            return;
        }

        bool should_place_order = false;
        if (trader_side_ == Order::Side::BID && rsi < 30 && (!use_stoch_rsi_ || stoch_rsi < 20))
        {
            should_place_order = true;
        }
        else if (trader_side_ == Order::Side::ASK && rsi > 70 && (!use_stoch_rsi_ || stoch_rsi > 80))
        {
            should_place_order = true;
        }
        
        if (should_place_order) 
        { 
            double price = getQuotePrice(rsi, stoch_rsi, best_bid, best_ask, trader_side_);
            placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
            std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << " | RSI: " << rsi << " | StochRSI: " << stoch_rsi << "\n";
        }
        else 
        { 
            std::cout << "Not placing order. RSI: " << rsi << " | StochRSI: " << stoch_rsi << "\n";
        }

    }

    double getQuotePrice(double rsi, double stoch_rsi, double best_bid, double best_ask, Order::Side trader_side_) 
    {
        double price; 
        double slippage = getRandom(-1, 1); // Small variation in price

        if (trader_side_ == Order::Side::BID) 
        { 
            price = best_ask; 
            price = std::min(limit_price_, price);
        } 

        else 
        { 
            price = best_bid; 
            price = std::max(limit_price_, price);
        }

        return price; 

    }

    void sleep()
    {
        std::uniform_real_distribution<> dist(-REL_JITTER, REL_JITTER);
        unsigned long jitter = dist(random_generator_);
        unsigned long sleep_time_ms = std::round(trade_interval_ms_ * (1.0+jitter));
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
    }

    // Calculate RSI based on closing prices
    double calculateRSI(const std::vector<double>& prices)
    {
        if (prices.size() < lookback_) {
            return 50.0; // Neutral RSI
        }

        //double upsum = 1.e-60, dnsm = 1.e-60; Avoid division by zero. Upsum = sum of upward price movements (gains). Dnsm = sum of downward price movements (losses)
        double upsum = 0.0, dnsm = 0.0;
        size_t initial_calculation_period = std::min(static_cast<size_t>(lookback_), prices.size());

        //Initial calculation of upsum and dnsm (lookback period); initial smoothed averages for upward and downward movements over lookback period. 
        for (size_t i = 1; i < initial_calculation_period; ++i) 
        {
            double diff = prices[i] - prices[i - 1]; //Price difference for each consecutive price pair
            if (diff > 0.0) 
                upsum += diff;
            else
                dnsm += -diff; 
        }

        // Averages of upward/downard price movements over lookback period
        upsum /= (lookback_ - 1); 
        dnsm /= (lookback_ - 1); 

        //Processes remaining prices sequentially to dynamically update smoothed averages using exponential smoothing
        for (size_t i = initial_calculation_period; i < prices.size(); ++i) 
        {   
            double diff = prices[i] - prices[i - 1]; //Price difference for each consecutive price pair
            if (diff > 0.0) //If diff > 0.0 its upward price movement
            {
                upsum = ((lookback_ - 1) * upsum + diff) / lookback_; 
                dnsm *= (lookback_ - 1.0) / lookback_;
            }
            else
            {
                dnsm = ((lookback_ - 1) * dnsm - diff) / lookback_; //Update dnsm using exponential smoothing. 
                upsum *= (lookback_ - 1.0) / lookback_; 
            }
        }
        
        if (upsum + dnsm < 1e-6) { // Prevents divison by 0 when calculating RSI by returning neutral value (50) if sum of upward and downward price movement is extremely small (less than 1e-6)
           return 50.0;
        } 

        return 100.0 * (upsum / (upsum + dnsm)); // Calculate RSI value based on smoothed averages of upward and downward price movements
    }

    double calculateStochRSI(const std::vector<double>& rsi_values, int stoch_lookback, int n_to_smooth)
    {
        size_t n = rsi_values.size();

        if (n < stoch_lookback) { // If no. of RSI values is less than required stochastic RSI lookback length; avoids error when not enough data 
            return 50.0; 
        }

        std::vector<double> stoch_rsi_values(n, 0.0); // Vector of zeros (same size as RSI values) to store computed S.RSI values

        // Initialise min and max RSI values for lookback period
        for (size_t icase = stoch_lookback - 1; icase < n; ++icase) { // Loop through existing RSI values
            double min_val = 1e60; // Arbitrary high value;
            double max_val = -1e60; 

            // Inner loop for lookback2 window
            for (size_t j = icase - stoch_lookback + 1; j <= icase; ++j) { // Loop through stochastic lookback RSI values (sliding window) 
                if (rsi_values[j] < min_val) min_val = rsi_values[j]; // Finds min. and max. RSI within this period 
                if (rsi_values[j] > max_val) max_val = rsi_values[j];
            }

            // Compute Stochastic RSI
            if (max_val == min_val) { // If max val == min. val -> no variation -> assign netural RSI value
                stoch_rsi_values[icase] = 50.0; 
            } else {
                stoch_rsi_values[icase] = 100.0 * (rsi_values[icase] - min_val) / (max_val - min_val); // Stochastic RSI formula; normalises RSI values between 0 and 100. Closer to 100 -> RSI is at upper range of past values. Closer to 0 -> RSI is at lower range of past values
            }
        }

        // Exponential smoothing (EMA) if requested (reduces noise in stochastic RSI for stability)
        if (n_to_smooth > 1) {
            double alpha = 2.0 / (n_to_smooth + 1.0);
            double smoothed = stoch_rsi_values[stoch_lookback - 1];
            for (size_t icase = stoch_lookback; icase < n; ++icase) { // Iterate over stochastic RSI values and apply smoothing EMA formula
                smoothed = alpha * stoch_rsi_values[icase] + (1.0 - alpha) * smoothed;
                stoch_rsi_values[icase] = smoothed;
            }
        }

        return stoch_rsi_values.back(); // Return the latest Stochastic RSI value
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
    int lookback_;
    bool cancelling_;
    unsigned int trade_interval_ms_;
    std::vector<double> closing_prices_;
    std::vector<double> rsi_values_; // Store RSI values for Stochastic RSI calculation

    std::optional<int> last_accepted_order_id_ = std::nullopt;

    std::mt19937 random_generator_;

    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;

    constexpr static double REL_JITTER = 0.25;

    bool use_stoch_rsi_; 
    int stoch_lookback_; 
    int n_to_smooth_; 

    constexpr static unsigned long MS_TO_NS = 1000000;
    unsigned long next_trade_timestamp_;
    std::optional<MarketDataPtr> last_market_data_;
    std::stack<CustomerOrderMessagePtr> customer_orders_;


};

#endif