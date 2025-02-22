#ifndef TRADER_SHVR_HPP
#define TRADER_SHVR_HPP

#include "traderagent.hpp"
#include "../message/profitmessage.hpp"

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
        sendProfitToExchange();
        is_trading_ = false;
        std::cout << "Trading window ended.\n";
    }

    void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) override
    {   
        std::cout << "Received market data from " << exchange << "\n";
        int quantity = 100;
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
        if (trader_side_ == Order::Side::BID)
        {
            double shaved_price = msg->data->best_bid + 1;
            return std::min(shaved_price, limit_price_);
        }
        else
        {
            double shaved_price = msg->data->best_ask - 1;
            return std::max(shaved_price, limit_price_);
        }
    }

    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;

    bool is_trading_ = false;

    constexpr static double MIN_PRICE = 1.0;
    constexpr static double MAX_PRICE = 200.0;
};

#endif