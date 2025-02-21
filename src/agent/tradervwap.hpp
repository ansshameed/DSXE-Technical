#ifndef TRADER_VWAP_HPP
#define TRADER_VWAP_HPP

#include <vector>
#include <numeric>
#include <algorithm>
#include "traderagent.hpp"

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

        double closing_price = msg->data->last_price_traded; // Closing price is the last price traded
        double volume = msg->data->volume_per_tick; // Volume is the last quantity traded

        price_volume_data_.emplace_back(closing_price, volume); // Store the most recent prices & volumes for rolling VWAP calculation

        if (price_volume_data_.size() > lookback_) // If the lookback period is exceeded, remove the oldest price-volume data
        {
            price_volume_data_.erase(price_volume_data_.begin()); // Remove the oldest price-volume data
        }

        double rolling_vwap = calculateVWAP(price_volume_data_); // Calculate the VWAP using the price-volume data (rolling VWAP window)
        std::cout << "Rolling VWAP: " << rolling_vwap << "\n"; 

        if (trader_side_ == Order::Side::BID && msg->data->last_price_traded < rolling_vwap) // If trader is a buyer and price is below VWAP, place BID order
        {
            std::cout << "Price below VWAP, placing BID order\n";
            placeOrder(Order::Side::BID, rolling_vwap);
        }
        else if (trader_side_ == Order::Side::ASK && msg->data->last_price_traded > rolling_vwap) // If trader is a seller and price is above VWAP, place ASK order
        {
            std::cout << "Price above VWAP, placing ASK order\n";
            placeOrder(Order::Side::ASK, rolling_vwap);
        }
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        if (msg->order->status == Order::Status::NEW)
        {
            last_accepted_order_id_ = msg->order->id;
        }

        if (msg->trade) { 
            // Cast to LimitOrder if needed
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

    void placeOrder(Order::Side side, double vwap_price)
    {
        if (cancelling_ && last_accepted_order_id_.has_value())
        {
            cancelOrder(exchange_, side, ticker_, last_accepted_order_id_.value());
            last_accepted_order_id_ = std::nullopt;
        }

        //int quantity = getRandomOrderSize();
        int quantity = 100; 

        double price_adjustment = 0.001 * vwap_price; // 0.1% price adjustment for slippage 
        double price = (side == Order::Side::BID) ? (vwap_price + price_adjustment) // If BID then increase price slightly, else decrease price slightly (ASK)
                                                  : (vwap_price - price_adjustment);

        placeLimitOrder(exchange_, side, ticker_, quantity, price, limit_price_);
        std::cout << ">> " << (side == Order::Side::BID ? "BID" : "ASK") 
                  << " " << quantity << " @ " << price << "\n";
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

    constexpr static double REL_JITTER = 0.25; // Lower jitter for higher frequency execution

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