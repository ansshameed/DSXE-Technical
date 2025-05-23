#ifndef TRADER_AGENT_HPP
#define TRADER_AGENT_HPP

#include <iostream>

#include "agent.hpp"
#include "../config/traderconfig.hpp"
#include "../trade/trade.hpp"
#include "../order/order.hpp"
#include "../message/market_data_message.hpp"
#include "../message/exec_report_message.hpp"
#include "../message/subscribe_message.hpp"
#include "../message/limit_order_message.hpp"
#include "../message/market_order_message.hpp"
#include "../message/cancel_order_message.hpp"
#include "../message/event_message.hpp"
#include "../message/cancel_reject_message.hpp"

class TraderAgent : public Agent
{
public:

    TraderAgent() = delete;
    virtual ~TraderAgent() = default;

    TraderAgent(NetworkEntity *network_entity, AgentConfigPtr config)
    : Agent(network_entity, std::static_pointer_cast<AgentConfig>(config))
    {
        if (auto trader_config = std::dynamic_pointer_cast<TraderConfig>(config))
        {
            exchange_ = trader_config->exchange_name;
        }
    }

    /** Gracefully terminates the trader, freeing all memory. */
    virtual void terminate() override;

    /** Subscribes to updates for the stock with the given ticker at the given exchange. */
    void subscribeToMarket(std::string_view exchange, std::string_view ticker);

    /** Places a limit order for the given ticker at the given exchange. */
    void placeLimitOrder(std::string_view exchange, Order::Side side, std::string_view ticker, int quantity, double price, double priv_value,
    Order::TimeInForce time_in_force = Order::TimeInForce::GTC, int client_order_id = 0);

    // /** Places a market order for the given ticker at the given exchange. */
    void placeMarketOrder(std::string_view exchange, Order::Side side, std::string_view ticker, int quantity, double priv_value);

    // /** Cancels the order with the given id at the given exchange. */
    void cancelOrder(std::string_view exchange, Order::Side side, std::string_view ticker, int order_id);

    /** The trader will remain idle and no handlers will be called until the specified duration after trading start. */
    void addDelayedStart(int delay_in_seconds);

    /** Returns a random price for the order. */
    int getRandomOrderSize();

    /** Get agent name for display purposes. */
    std::string agent_name_;
    virtual std::string getAgentName() const;

    /** Check if agent is a legacy agent. */
    bool isLegacyTrader() const;

protected:

    /** Derived classes must implement these: */

    /** The callback function called when trading window starts. */
    virtual void onTradingStart() = 0;

    /** The callback function called when trading window ends. */
    virtual void onTradingEnd() = 0;

    /** The callback function called when new market data update is received. */
    virtual void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) = 0;

    /** The callback function called when the execution report message is received. */
    virtual void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) = 0;

    /** The callback function called when the cancel order message is rejected. */
    virtual void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) = 0;

    /** Bookkeeping trades for profit calculations. */
    void bookkeepTrade(const TradePtr & trade, const LimitOrderPtr & order);

    /** Checks the type of the incoming message and makes a callback. */
    std::optional<MessagePtr> handleMessageFrom(std::string_view sender, MessagePtr message) override;

    /** Checks the type of the incoming broadcast and makes a callback. */
    void handleBroadcastFrom(std::string_view sender, MessagePtr message) override;

    /** Bookkeeping function for individual traders */
    unsigned int n_trades; 
    std::vector<TradePtr> blotter_; 
    double balance = 0.0; 
    
    /** Steady state for legacy vs technical trading agents. */
    bool is_legacy_trader_ = false;
    bool balance_reset_performed = false; // Whether balance reset has been performed 
    static constexpr unsigned int TECHNICAL_AGENT_DELAY_SECONDS = 4; // 2 second delay for technical agents
    static bool technical_agents_started_; // Static flag to track if technical agents have started

    /** Name of exchange the trader is connecting to */
    std::string exchange_; 

private:

    /** Signals that trading has started and starts sending callbacks to handlers. */
    void signalTradingStart();

    /** Broadcast message to inform agents that technical indicators have started trading. */
    void broadcastTechnicalTradingStart();

    /** TODO: Signals that trading has ended and stops sending callbacks to handlers. */
    // void signalTradingEnd();

    /** Used for delayed start of the trader. */
    bool trading_window_open_ = false;
    unsigned int start_delay_in_seconds_ = 0;
    std::mutex mutex_;
    std::thread* delay_thread_;
    
};

#endif