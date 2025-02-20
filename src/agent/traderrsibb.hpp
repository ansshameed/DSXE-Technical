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
        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);
    }

    std::string getAgentName() const override { return "RSI & Bollinger Bands"; }

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
        is_trading_ = false;
        std::cout << "Trading window ended.\n";
        lock.unlock();
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

        double last_price = msg->data->last_price_traded;

        // Maintain separate buffers for RSI and Bollinger Bands
        rsi_prices_.push_back(last_price); // Add last price traded to RSI price buffer
        bb_prices_.push_back(last_price); // Add last price traded to Bollinger Bands price buffer

        if (rsi_prices_.size() > lookback_rsi_) { // If RSI price buffer exceeds lookback period, remove oldest price
            rsi_prices_.erase(rsi_prices_.begin());
        }

        if (bb_prices_.size() > lookback_bb_) { // If Bollinger Bands price buffer exceeds lookback period, remove oldest price
            bb_prices_.erase(bb_prices_.begin());
        }

        // Calculate RSI
        double rsi = 50.0; // Neutral RSI
        if (rsi_prices_.size() >= lookback_rsi_) { // If RSI price buffer size exceeds lookback period, calculate RSI
            rsi = calculateRSI(rsi_prices_);
            std::cout << "RSI: " << rsi << "\n";
        }

        // Calculate Bollinger Bands
        if (bb_prices_.size() >= lookback_bb_) { // If Bollinger Bands price buffer size exceeds lookback period, calculate Bollinger Bands
            double sma = calculateSMA(bb_prices_); // Calculate Simple Moving Average
            double std_dev = calculateStandardDeviation(bb_prices_, sma); // Calculate Standard Deviation
            double upper_band = sma + (std_dev_multiplier_ * std_dev); // Calculate Upper Band
            double lower_band = sma - (std_dev_multiplier_ * std_dev); // Calculate Lower Band

            std::cout << "SMA: " << sma << ", Upper Band: " << upper_band << ", Lower Band: " << lower_band << "\n";

            // Trading logic based on RSI and Bollinger Bands
            if (trader_side_ == Order::Side::BID && last_price < lower_band && rsi < 30) // If trader is a buyer and last price traded is below lower band and RSI is less than 30
            {
                std::cout << "Price below lower Bollinger Band and RSI < 30, placing BID order\n";
                placeOrder(Order::Side::BID, lower_band); // Place BID order with lower band price
            }
            else if (trader_side_ == Order::Side::ASK && last_price > upper_band && rsi > 70) // If trader is a seller and last price traded is above upper band and RSI is greater than 70
            {
                std::cout << "Price above upper Bollinger Band and RSI > 70, placing ASK order\n";
                placeOrder(Order::Side::ASK, upper_band); // Place ASK order with upper band price
            }
        }
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        if (msg->order->status == Order::Status::NEW) // If order status is new (order added to order book)
        {
            last_accepted_order_id_ = msg->order->id; // Store the order ID
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

    double calculateSMA(const std::vector<double>& prices) // Calculate Simple Moving Average
    {
        double sum = std::accumulate(prices.begin(), prices.end(), 0.0); // Sum all elements in prices (begin and end define range to sum over). 0.0 = initial value of sum
        return sum / prices.size(); // Calculate Simple Moving Average; sum divided by number of elements in prices
    }

    double calculateStandardDeviation(const std::vector<double>& prices, double sma) // Calculate Standard Deviation
    {
        double sum = 0.0; // Initialise sum to 0
        for (double price : prices) // For each price in prices
        {
            sum += (price - sma) * (price - sma); // Add square of difference between price and SMA to sum (sum of squared differences)
        }
        return std::sqrt(sum / (prices.size() - 1)); // Calculate Standard Deviation; square root of sum divided by number of elements in prices minus 1; sample variance
    }

    double calculateRSI(const std::vector<double>& prices) // Calculate RSI based on closing prices
    {
        if (prices.size() < lookback_rsi_) { // If prices size less than lookback period, return neutral RSI
            return 50.0; // Neutral RSI
        }

        double upsum = 0.0, dnsm = 0.0; // Initialise upsum and dnsm to 0 (gain and loss)
        size_t initial_calculation_period = std::min(static_cast<size_t>(lookback_rsi_), prices.size()); // Initial calculation period (minimum of lookback period and prices size)

        for (size_t i = 1; i < initial_calculation_period; ++i) // Loop through lookback period 
        {
            double diff = prices[i] - prices[i - 1]; // Price difference for each consecutive price pair
            if (diff > 0.0) // If diff > 0.0 its upward price movement so add diff to upsum
                upsum += diff; // Add diff to upsum
            else
                dnsm += -diff; // If diff < 0.0 its downward price movement so add -diff to dnsm
        }
        upsum /= (lookback_rsi_ - 1); // Average of upward price movements (over lookback period)
        dnsm /= (lookback_rsi_ - 1); // Average of downward price movements (over lookback period)

        //RSI continuously updated using exponential smoothing (historical trend via upsum and dnsm and the new prices differences)
        for (size_t i = initial_calculation_period; i < prices.size(); ++i) // Loop through remaining closing prices and update upsum and dnsm (prices after lookback period)
        {
            double diff = prices[i] - prices[i - 1]; // Price difference for each consecutive price pair
            if (diff > 0.0) // If diff > 0.0 its upward price movement
            {
                upsum = ((lookback_rsi_ - 1) * upsum + diff) / lookback_rsi_; // Update upsum using exponential smoothing. Combine historical trend (prev. smoothed avg) with new price difference (new gain)
                dnsm *= (lookback_rsi_ - 1.0) / lookback_rsi_; // Update dnsm using exponential smoothing. Scales down dnsm to reflect new price difference (new gain)
            }
            else
            {
                dnsm = ((lookback_rsi_ - 1) * dnsm - diff) / lookback_rsi_; // Update dnsm using exponential smoothing. Combine historical trend (prev. smoothed avg) with new price difference (new loss)
                upsum *= (lookback_rsi_ - 1.0) / lookback_rsi_; // Update upsum using exponential smoothing. Scales down upsum to reflect new price difference (new loss)
            }
        }

        if (upsum + dnsm < 1e-6) { // Prevents divison by 0 when calculating RSI by returning neutral value (50) if sum of upward and downward price movement is extremely small (less than 1e-6)
            return 50.0; // Neutral RSI
        }

        double rsi = 100.0 * (upsum / (upsum + dnsm)); // Calculate RSI; normalises ratio of avg. gains (upsum) to total movement (upsum + dnsm) to 0-100 scale. RSI < 30: oversold, RSI > 70: overbought
        return rsi;
    }

    void placeOrder(Order::Side side, double band_price)
    {
        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, side, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }

        int quantity = getRandomOrderSize();

        double price_adjustment = 0.002 * band_price; // 0.2% of price as slippage. Slippage = difference between expected price and actual price
        double price = (side == Order::Side::BID) ? (band_price + price_adjustment) // If BID then increase price slightly, else decrease price slightly (ASK)
                                                  : (band_price - price_adjustment);

        placeLimitOrder(exchange_, side, ticker_, quantity, price, limit_price_); 

        std::cout << ">> " << (side == Order::Side::BID ? "BID" : "ASK") 
                  << " " << quantity << " @ " << price << "\n";
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
    std::vector<double> closing_prices_;

    std::optional<int> last_accepted_order_id_ = std::nullopt;

    std::mt19937 random_generator_;

    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;

    std::vector<double> rsi_prices_;  // Stores prices for RSI calculation
    std::vector<double> bb_prices_;   // Stores prices for Bollinger Bands calculation

    constexpr static double REL_JITTER = 0.25;

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