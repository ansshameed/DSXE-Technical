#ifndef TRADER_RSI_HPP
#define TRADER_RSI_HPP

#include <vector>
#include <numeric>
#include <algorithm>
#include "traderagent.hpp"

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
      use_stoch_rsi_{use_stoch_rsi}, // Initialise Stochastic RSI
      stoch_lookback_{stoch_lookback}, // Initialise Stochastic RSI lookback period
      n_to_smooth_{n_to_smooth}, // Initialise smoothing factor
      random_generator_{std::random_device{}()},
      profit_margin_{0.0}, // Initialise profit margin
      mutex_{}
    {
        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);
    }

    std::string getAgentName() const override { return "RSI"; }

    void onTradingStart() override
    {
        std::cout << "Trading window started.\n";
        is_trading_ = true;
        activelyTrade();
    }

    void onTradingEnd() override {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            is_trading_ = false;
        } 
        sendProfitToExchange();
        std::cout << "Trading window ended.\n";
        std::cout << "Final profit: " << balance << "\n";
    }

    void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) override
    { 
        std::unique_lock<std::mutex> lock(mutex_);
        if (!is_trading_) 
        { 
            return; 
        }
        lock.unlock(); 

        std::cout << "Received market data from " << exchange << "\n";

        std::cout << "Last price traded: " << msg->data->last_price_traded << "\n";

        // Collect closing prices from market data (prices broadcasted; market data received from exchange contains last traded price)
        closing_prices_.push_back(msg->data->last_price_traded); //rolling list stores most recent closing prices upto lookback period (past 14 closing prices; the last prices traded)
        if (closing_prices_.size() > lookback_ + 1) //Adds the last price traded from market data to buffer
        {
            closing_prices_.erase(closing_prices_.begin()); //If buffer exceeds lookback, remove the oldest price
        }

        // Calculate RSI (only when no. of closing prices is equal to lookback)
        if (closing_prices_.size() >= lookback_)
        {
            double rsi = calculateRSI(closing_prices_); //Calculates RSI based on closing prices
            std::cout << "RSI: " << rsi << "\n";

            if (use_stoch_rsi_) { // If using Stochastic RSI is activated (true) 
                // Store RSI values for Stochastic RSI calculation
                rsi_values_.push_back(rsi);

                if (rsi_values_.size() > stoch_lookback_) {  // If more RSI values than length of Stochastic RSI lookback
                    rsi_values_.erase(rsi_values_.begin());  // If buffer exceeds lookback, remove the oldest price 
                }

                // Calculate Stochastic RSI 
                double stoch_rsi = calculateStochRSI(rsi_values_, stoch_lookback_, n_to_smooth_); // Calculate Stochastic RSI value based on previously calculated RSI values, Stochastic lookback length and smoothing factor 
                std::cout << "Stochastic RSI: " << stoch_rsi << "\n";

                if (trader_side_ == Order::Side::BID && stoch_rsi < 20) { // If Stochastic RSI is less than 20 then buy signal generated (oversold) 
                    std::cout << "Oversold detected, placing BID order\n";
                    placeOrder(Order::Side::BID); // Oversold condition
                } 
                else if (trader_side_ == Order::Side::ASK && stoch_rsi > 80) { // If Stochastic RSI is more than 80 then sell signal generated (overbought) 
                    std::cout << "Overbought detected, placing ASK order\n";
                    placeOrder(Order::Side::ASK); // Overbought condition
                }

            } else {
                // Implement trading logic based on RSI
                if (trader_side_ == Order::Side::BID && rsi < 30) //If RSI is less than 30, buy signal is generated (oversold)
                {
                    // Buy signal
                    placeOrder(Order::Side::BID);
                }
                else if (trader_side_ == Order::Side::ASK && rsi > 70) //If RSI is greater than 70, sell signal is generated (overbought)
                {
                    // Sell signal
                    placeOrder(Order::Side::ASK);
                } 
            }
        }
    }

    //Handles execution report received from exchange when status update for order. Msg = param containing order details
    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        // Order added to order book
        if (msg->order->status == Order::Status::NEW) //Checks if order is new (NEW = order successfully added to LOB by exchange)
        {
            last_accepted_order_id_ = msg->order->id; //Tracks most recent order successfully added to LOB (can be used later for cancel/modifying)
        }

    }

    void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) override
    {
        std::cout << "Received cancel reject from " << exchange << ": Order: " << msg->order_id;
    }

private:

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

                sleep();
                lock.lock();
            }
            lock.unlock();

            std::cout << "Finished actively trading.\n";
        });
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

    // Calculate RSI based on closing prices
    double calculateRSI(const std::vector<double>& prices)
    {
        if (prices.size() < lookback_) {
            return 50.0; // Neutral RSI
        }

        //double upsum = 1.e-60, dnsm = 1.e-60; //Avoid division by zero. Upsum = sum of upward price movements (gains). Dnsm = sum of downward price movements (losses)
        double upsum = 0.0, dnsm = 0.0;
        size_t initial_calculation_period = std::min(static_cast<size_t>(lookback_), prices.size());

        //Initial calculation of upsum and dnsm (lookback period); initial smoothed averages for upward and downward movements over lookback
        //RSI initially calculated on lookback period
        for (size_t i = 1; i < initial_calculation_period; ++i) //Loop through lookback period (14 closing prices)
        {
            double diff = prices[i] - prices[i - 1]; //Price difference for each consecutive price pair
            if (diff > 0.0) //If diff > 0.0 its upward price movement so add diff to upsum
                upsum += diff;
            else //If diff < 0.0 its downward price movement so add diff to dnsm
                dnsm += -diff; 
        }
        upsum /= (lookback_ - 1); //Average of upward price movements (over lookback period)
        dnsm /= (lookback_ - 1); //Average of downward price movements (over lookback period)

        //Processes remaining prices sequentially to dynamically update smoothed averages using exponential smoothing
        //RSI continuously updated using exponential smoothing (historical trend via upsum and dnsm and the new prices differences)
        for (size_t i = initial_calculation_period; i < prices.size(); ++i) //Loop through remaining closing prices and update upsum and dnsm (prices after lookback period)
        {   
            double diff = prices[i] - prices[i - 1]; //Price difference for each consecutive price pair
            //std::cout << "Processing price: " << prices[i] << ", Previous price: " << prices[i - 1] << ", Difference: " << diff << "\n"; // Print the price difference
            if (diff > 0.0) //If diff > 0.0 its upward price movement
            {
                upsum = ((lookback_ - 1) * upsum + diff) / lookback_; //Update upsum using exponential smoothing. Combine historical trend (prev. smoothed avg) with new price difference (new gain)
                dnsm *= (lookback_ - 1.0) / lookback_; //Update dnsm using exponential smoothing. Scales down dnsm to reflect new price difference (new gain)
            }
            else
            {
                dnsm = ((lookback_ - 1) * dnsm - diff) / lookback_; //Update dnsm using exponential smoothing. Combine historical trend (prev. smoothed avg) with new price difference (new loss)
                upsum *= (lookback_ - 1.0) / lookback_; //Update upsum using exponential smoothing. Scales down upsum to reflect new price difference (new loss)
            }
        }
        
        if (upsum + dnsm < 1e-6) { // Prevents divison by 0 when calculating RSI by returning neutral value (50) if sum of upward and downward price movement is extremely small (less than 1e-6)
           return 50.0;
        } 

        double rsi = 100.0 * (upsum / (upsum + dnsm)); // Calculate RSI; normalises ratio of avg. gains (upsum) to total movement (upsum + dnsm) to 0-100 scale. RSI < 30: oversold, RSI > 70: overbought
        return rsi; 
    }

    void placeOrder(Order::Side side) //Places limit order on exchange 
    {

        //NOTE - COOLDOWN PERIOD: maybe add cooldown period mechanism for when excessive trades happen (only one trade within cooldown period; useful for extreme RSI values i.e. 0 or 100)
        //std::chrono::steady_clock::time_point last_trade_time_; PRIVATE VARIABLE 
        //unsigned int cooldown_duration_ms_ = 5000; // Cooldown period in milliseconds (e.g., 5 seconds) PRIVATE VARIABLE 
        //auto now = std::chrono::steady_clock::now();
        //if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_trade_time_.count() < cooldown_duration_ms_)
        //{
            //std::cout << "Skipping trade due to cooldown period." << std::endl;
            //return; // Exit function if still in cooldown
        //}


        if (cancelling_ && last_accepted_order_id_.has_value()) //Check if cancelling is enabled and last order was accepted
        {
            cancelOrder(exchange_, side, ticker_, last_accepted_order_id_.value()); //Cancel last accepted order
            last_accepted_order_id_ = std::nullopt; //Clears last accepted order to null
        }

        int quantity = 100; //Order fixed at 100 quantity
        //int quantity = getRandomOrderSize(); // Use random order size
        double price = getQuotePrice(side); //Get quote price based on trading side (BID or ASK)    
        placeLimitOrder(exchange_, side, ticker_, quantity, price, limit_price_); //Place limit order on exchange

        std::cout << ">> " << (side == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << "\n"; //Logs the order
    }

    double getQuotePrice(Order::Side side) //Adjust for side
    {
        double price = round(limit_price_ * (1 + profit_margin_)); //Adjust limit price based on profit margin (positive margin = increase ASK order, negative margin = decrease BID order)
        if (side == Order::Side::BID) //If BID order, adjust price to be less than limit price
        {
            price = std::min(limit_price_, price);
        }
        else //If ASK order, adjust price to be greater than limit price 
        {
            price = std::max(limit_price_, price);
        }
        return price; //Return adjusted price for limit order
    }

    double calculateStochRSI(const std::vector<double>& rsi_values, int stoch_lookback, int n_to_smooth)
    {
        size_t n = rsi_values.size();

        if (n < stoch_lookback) { // If no. of RSI values is less than required stochastic RSI lookback length; avoids error when not enough data 
            return 50.0; // Handle small buffer with neutral 50 RSI value (neutral)
        }

        std::vector<double> stoch_rsi_values(n, 0.0); // Vector of zeros (same size as RSI values) to store computed S.RSI values

        // Initialize min and max RSI values for lookback period
        for (size_t icase = stoch_lookback - 1; icase < n; ++icase) { // Loop through existing RSI values
            double min_val = 1e60; // Arbitrary high value; extreme values to store the min. and max. RSI values in the lookback period 
            double max_val = -1e60; // Arbitrary low value

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

    double profit_margin_; 
    constexpr static double REL_JITTER = 0.25;

    bool use_stoch_rsi_; 
    int stoch_lookback_; 
    int n_to_smooth_; 

    // Profitability parameters 
    struct Trade { 
        double price;
        int quantity;
        Order::Side side;
    }; 
    std::vector<Trade> executed_trades_; 
    double total_profit_ = 0.0;
};

#endif