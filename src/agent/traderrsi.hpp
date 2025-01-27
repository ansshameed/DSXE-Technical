#ifndef TRADER_RSI_HPP
#define TRADER_RSI_HPP

#include <vector>
#include <numeric>
#include <algorithm>
#include "traderagent.hpp"

class TraderRSI : public TraderAgent
{
public:

    TraderRSI(NetworkEntity *network_entity, TraderConfigPtr config, int lookback)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      lookback_{lookback},
      cancelling_{config->cancelling},
      trade_interval_ms_{config->trade_interval},
      random_generator_{std::random_device{}()},
      mutex_{},
      profit_margin_{0.0} // Initialise profit margin
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
        std::cout << "Last price traded: " << msg->data->last_price_traded << "\n";

        // Collect closing prices from market data (prices broadcasted; market data received from exchange contains last traded price)
        closing_prices_.push_back(msg->data->last_price_traded); //rolling list stores most recent closing prices upto lookback period (past 14 closing prices; the last prices traded)
        if (closing_prices_.size() > lookback_) //Adds the last price traded from market data to buffer
        {
            closing_prices_.erase(closing_prices_.begin()); //If buffer exceeds lookback, remove the oldest price
        }

        // Calculate RSI (only when no. of closing prices is equal to lookback)
        if (closing_prices_.size() == lookback_)
        {
            double rsi = calculateRSI(closing_prices_); //Calculates RSI based on closing prices
            std::cout << "RSI: " << rsi << "\n";

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
        double upsum = 1.e-60, dnsm = 1.e-60; //Avoid division by zero. Upsum = sum of upward price movements (gains). Dnsm = sum of downward price movements (losses)

        //Initial calculation of upsum and dnsm (lookback period); initial smoothed averages for upward and downward movements over lookback
        //RSI initially calculated on lookback period
        for (size_t i = 1; i < lookback_; ++i) //Loop through lookback period (14 closing prices)
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
        for (size_t i = lookback_; i < prices.size(); ++i) //Loop through remaining closing prices and update upsum and dnsm (prices after lookback period)
        {
            double diff = prices[i] - prices[i - 1]; //Price difference for each consecutive price pair
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
        return 100.0 * upsum / (upsum + dnsm); //Calculate RSI; normalises ratio of avg. gains (upsum) to total movement (upsum + dnsm) to 0-100 scale. RSI < 30: oversold, RSI > 70: overbought
    }

    void placeOrder(Order::Side side) //Places limit order on exchange 
    {
        if (cancelling_ && last_accepted_order_id_.has_value()) //Check if cancelling is enabled and last order was accepted
        {
            cancelOrder(exchange_, side, ticker_, last_accepted_order_id_.value()); //Cancel last accepted order
            last_accepted_order_id_ = std::nullopt; //Clears last accepted order to null
        }

        int quantity = 100; //Order fixed at 100 quantity
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

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    int lookback_;
    bool cancelling_;
    unsigned int trade_interval_ms_;
    std::vector<double> closing_prices_;

    std::optional<int> last_accepted_order_id_ = std::nullopt;

    std::mt19937 random_generator_;

    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;

    double profit_margin_; 
    constexpr static double REL_JITTER = 0.25;
};

#endif
