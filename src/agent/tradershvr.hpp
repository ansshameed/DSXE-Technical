#ifndef TRADER_SHVR_HPP
#define TRADER_SHVR_HPP

#include "traderagent.hpp"
#include "../message/profitmessage.hpp"
#include "../message/customer_order_message.hpp"

class TraderShaver : public TraderAgent
{
public:

    TraderShaver(NetworkEntity *network_entity, TraderConfigPtr config)
    : TraderAgent(network_entity, config),
      exchange_{config->exchange_name},
      ticker_{config->ticker},
      trader_side_{config->side},
      limit_price_{config->limit}
    {   
        
        // Mark as legacy agent
        is_legacy_trader_ = true;

        // Automatically connect to exchange on initialisation
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            subscribeToMarket(config->exchange_name, config->ticker);
        });

        // Add delayed start
        addDelayedStart(config->delay);
    }

    std::string getAgentName() const override { return "SHVR"; }

    void onTradingStart() override
    {
        std::cout << "Trading window started.\n";
        is_trading_ = true;
    }

    void onTradingEnd() override
    {   
        //sendProfitToExchange();
        is_trading_ = false;
        std::cout << "Trading window ended.\n";
    }

    void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) override
    {   
        std::cout << "Received market data from " << exchange << "\n";
        std::uniform_int_distribution<int> dist(10, 50);
        int quantity = dist(random_generator_);
        if (is_trading_) 
        {
            double price = getShaverPrice(msg);
            placeLimitOrder(exchange_, trader_side_, ticker_, quantity, price, limit_price_);
            std::cout << ">> " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") << " " << quantity << " @ " << price << "\n";
        }
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {   

        if (msg->trade) { 
            // Cast to LimitOrder if needed
            LimitOrderPtr limit_order = std::dynamic_pointer_cast<LimitOrder>(msg->order);
            if (!limit_order) {
                throw std::runtime_error("Failed to cast order to LimitOrder.");
            }
            bookkeepTrade(msg->trade, limit_order);
        }

        std::cout << "Received execution report from " << exchange << ": Order: " << msg->order->id << " Status: " << msg->order->status << 
        " Qty remaining = " << msg->order->remaining_quantity << "\n";
    }

    void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) override
    {
        throw std::runtime_error("Shaver trader does not cancel orders therefore cannot receive cancel rejection.");
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
                std::cout << "[SHVR] Enqueued CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
            }
            return; // Just exit the function
        }
        // If it's not a customer order, call the base class handler
        TraderAgent::handleBroadcastFrom(sender, message);
    }


private:

    void sendProfitToExchange()
    {
        ProfitMessagePtr profit_msg = std::make_shared<ProfitMessage>();
        profit_msg->agent_name = getAgentName(); 
        profit_msg->profit = balance; 
        sendMessageTo(exchange_, std::dynamic_pointer_cast<Message>(profit_msg), true);
    }

    double getShaverPrice(MarketDataMessagePtr msg)
    {
        double price;

        if (!customer_orders_.empty())
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_customer_order_ = customer_orders_.top();
            customer_orders_.pop();
            limit_price_ = current_customer_order_.value()->price;
        }

        if (trader_side_ == Order::Side::BID)
        {
            price = std::min(msg->data->best_bid + 1, limit_price_);
        }
        else
        {
            price = std::max(msg->data->best_ask - 1, limit_price_);
        }

        return price;
    }


    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;

    bool is_trading_ = false;

    constexpr static double MIN_PRICE = 1.0;
    constexpr static double MAX_PRICE = 200.0;

    std::optional<CustomerOrderMessagePtr> current_customer_order_;
    std::mutex mutex_;
    std::stack<CustomerOrderMessagePtr> customer_orders_;

    std::mt19937 random_generator_;
};

#endif