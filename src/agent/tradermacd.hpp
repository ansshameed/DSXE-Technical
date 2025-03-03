#ifndef TRADER_MACD_HPP
#define TRADER_MACD_HPP

#include "traderagent.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

class TraderMACD : public TraderAgent
{
public:
    TraderMACD(NetworkEntity *network_entity, TraderConfigPtr config, int short_length, int long_length, int signal_length, double threshold, int n_to_smooth, int lookback_period)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit},
      cancelling_{config->cancelling},
      trade_interval_ms_{config->trade_interval},
      short_length_{short_length},
      long_length_{long_length},
      signal_length_{signal_length},
      threshold_{threshold},
      n_to_smooth_{n_to_smooth},
      lookback_period_{lookback_period},
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

    std::string getAgentName() const override { return "MACD"; }

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
                std::cout << "[MACD] Enqueued CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
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

                if (prices_.size() >= lookback_period_) 
                {
                    // Compute MACD (and signal) on the local copy.
                    auto [macd_line, signal_line] = calculateMACD(prices_);
                    
                    // Additional safety check
                    if (!macd_line.empty() && !signal_line.empty())
                    {
                        double macd = macd_line.back();
                        double signal = signal_line.back();
                        double histogram = macd - signal;
                        
                        std::cout << "MACD: " << macd 
                                << ", Signal: " << signal 
                                << ", Histogram: " << histogram 
                                << ", Threshold: " << threshold_ 
                                << "\n";
                        
                        placeOrder(histogram, prices_.back());
                    }
                }

                sleep();
                lock.lock();
            }
            lock.unlock();
            std::cout << "Finished actively trading.\n";
        });
    }

    void placeOrder(double histogram, double last_price)
    {

        std::cout << "Order Placement Attempt:\n"
        << "Histogram: " << histogram << "\n"
        << "Threshold: " << threshold_ << "\n"
        << "Trader Side: " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << "\n"
        << "Last Market Data: " << (last_market_data_.has_value() ? "Available" : "NOT Available") << "\n";
        
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
        }

        double best_bid = last_market_data_.value()->best_bid;
        double best_ask = last_market_data_.value()->best_ask;
        std::uniform_int_distribution<int> dist(10, 50);
        int quantity = dist(random_generator_);

        bool should_place_order = false;
        std::cout << "Trade conditions: ";
        if (trader_side_ == Order::Side::BID && histogram > threshold_) 
        {
            should_place_order = true;
        }
        else if (trader_side_ == Order::Side::ASK && histogram < -threshold_)
        {
            should_place_order = true;
        }

        if (should_place_order) 
        {
            double price = getQuotePrice(best_bid, best_ask);
            placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
            std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << " (MACD Histogram: " << histogram << " | Best Bid: " << best_bid << " | Best Ask: " << best_ask << ")\n";
        }
        else
        {
            std::cout << "Trade conditions NOT met. No order placed.\n";
        }


    }

    // Determines a quote price based on the MACD value and current bid/ask, applying a slippage ("shaver").
    double getQuotePrice(double best_bid, double best_ask)
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
        double price = msg->data->last_price_traded;
        prices_.push_back(price);
        highs_.push_back(msg->data->high_price);
        lows_.push_back(msg->data->low_price);
        closes_.push_back(price);
        updateRollingWindow(msg->data->high_price, msg->data->low_price);

        if (prices_.size() > lookback_period_)
        {
            prices_.erase(prices_.begin());
            highs_.erase(highs_.begin());
            lows_.erase(lows_.begin());
            closes_.erase(closes_.begin());
        }

        last_market_data_ = msg->data;
        std::cout << "Stored Market Data - Price: " << price << ", Prices size: " << prices_.size() << "\n";
    }

    void updateRollingWindow(double high_price, double low_price)
    {
        high_window_.push_back(high_price);
        low_window_.push_back(low_price);
        if (high_window_.size() > lookback_period_)
        {
            high_window_.pop_front();
            low_window_.pop_front();
        }
    }

    std::pair<std::vector<double>, std::vector<double>> calculateMACD(const std::vector<double>& prices_)
    {   
        // Store MACD and signal line values for each price point
        std::vector<double> macd_line(prices_.size(), 0.0); // Initialise MACD line with zeros 
        std::vector<double> signal_line(prices_.size(), 0.0); // Initialise signal line with zeros
        if(prices_.empty()) return {macd_line, signal_line};

        double long_sum = prices_[0]; // Initialise long EMA sum with first price (26-period)
        double short_sum = prices_[0]; // Initialise short EMA sum with first price (12-period)
        double long_alpha = 2.0 / (long_length_ + 1.0); // Calculate alpha for long EMA
        double short_alpha = 2.0 / (short_length_ + 1.0);  // Calculate alpha for short EMA
        double signal_alpha = 2.0 / (signal_length_ + 1.0); // Calculate alpha for signal line (9-period)

        // Calculate MACD line and EMAs for each price point. 
        for (int icase = 1; icase < prices_.size(); ++icase) // Loop through each price point
        {
            long_sum = long_alpha * prices_[icase] + (1.0 - long_alpha) * long_sum; // Calculate long EMA
            short_sum = short_alpha * prices_[icase] + (1.0 - short_alpha) * short_sum; // Calculate short EMA
            double diff = 0.5 * (long_length_ - 1.0) - 0.5 * (short_length_ - 1.0); // Calculate difference between long and short EMA lengths
            double denom = std::sqrt(std::abs(diff)); // Normalisation factor for MACD using EMA difference
            denom *= ATR(icase, lookback_period_);
            if (std::abs(denom) < 1e-10) // Avoid divide-by-zero
                denom = 1e-10;  
            macd_line[icase] = (short_sum - long_sum) / (denom + 1.e-15); // MACD normalisation 
            macd_line[icase] = 100.0 * normal_cdf(1.0 + macd_line[icase]) - 50.0; // Normalise MACD to 0-100 scale (applies normal cumulative distributin function to MACD; CDF) 

            //std::cout << "Price[" << icase << "]: " << prices_[icase] << ", Short EMA: " << short_sum << ", Long EMA: " << long_sum << ", MACD: " << macd_line[icase] << "\n";
        }

        signal_line[0] = macd_line[0]; // Calculate the signal line as an EMA of the MACD line
        for (int icase = 1; icase < macd_line.size(); ++icase) // Loop through each price point
        {
            signal_line[icase] = signal_alpha * macd_line[icase] + (1.0 - signal_alpha) * signal_line[icase - 1]; // Calculate signal line as EMA of MACD line using signal_alpha as smoothing factor
        }

        if (n_to_smooth_ > 1) // If additional smoothing steps are required
        {
            double alpha = 2.0 / (n_to_smooth_ + 1.0); // Calculate alpha for additional smoothing
            double smoothed = macd_line[0]; // Initialise smoothed MACD line with first MACD value
            for (int icase = 1; icase < macd_line.size(); ++icase) // Loop through each price point
            {
                smoothed = alpha * macd_line[icase] + (1.0 - alpha) * smoothed; // Apply additional smoothing to MACD line
                macd_line[icase] -= smoothed; // Subtract smoothed MACD line from MACD line
            }
        }

        return {macd_line, signal_line}; // Return MACD line and signal line
    }


    double ATR(int end, int lookback)
    {
        if (end < lookback) { // If not enough values to calculate ATR
            lookback = end + 1; // Adjust lookback if not enough values to include all available data points 
        }
    
        double atr = 0.0; // Initialise ATR with zero (accumulate total true range over lookback period)
        int start = end - lookback + 1; // Calculate start index for ATR calculation for rolling window 

        for (int i = start; i <= end; ++i) // Loop through rolling window to calculate the TR (True Range) for each period and accumulate total ATR
        {
            //high_window_[i - start] = highs_[i]; = high price for the current time period, low_window_[i - start] = lows_[i]; = low price for the current time period, closes_[i - 1] = closes_[i - 1]; = closing price for the previous time period
            double high_low = high_window_[i - start] - low_window_[i - start]; // High-low range for the current time period (difference between high and low prices for current period)
            double high_close = std::abs(high_window_[i - start] - closes_[i - 1]); // Abs. difference between current high price and previous closing price
            double low_close = std::abs(low_window_[i - start] - closes_[i - 1]); // Abs. difference between current low price and previous closing price
            atr += std::max({high_low, high_close, low_close}); // Calculate the True Range (TR) for each period and accumulate total ATR
            //std::cout << "ATR: " << atr << "\n";
        }

        return atr / lookback; // Return the average ATR over the lookback period
    }

    // Calculate the normal CDF for given value 
    double normal_cdf(double value)
    {  
        return 0.5 * std::erfc(-value / std::sqrt(2.0)); // Gaussian CDF (error function) for normal distribution. Result scaled by 0.5 to normalise to 0-1 scale
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

    // Member variables.
    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    bool cancelling_;
    unsigned int trade_interval_ms_;
    int short_length_;
    int long_length_;
    int signal_length_;
    double threshold_;
    int n_to_smooth_;
    int lookback_period_;
    std::vector<double> prices_;
    std::vector<double> highs_;
    std::vector<double> lows_;
    std::vector<double> closes_;
    std::deque<double> high_window_;
    std::deque<double> low_window_;
    std::optional<int> last_accepted_order_id_ = std::nullopt;
    std::optional<MarketDataPtr> last_market_data_;
    std::mt19937 random_generator_;
    std::mutex mutex_;
    std::thread* trading_thread_ = nullptr;
    bool is_trading_ = false;
    unsigned long next_trade_timestamp_;
    constexpr static double REL_JITTER = 0.25;
    constexpr static unsigned long MS_TO_NS = 1000000;
    std::stack<CustomerOrderMessagePtr> customer_orders_;
};

#endif