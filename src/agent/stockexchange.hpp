#ifndef STOCK_EXCHANGE_HPP
#define STOCK_EXCHANGE_HPP

#include <random>
#include <unordered_set>

#include "../agent/agent.hpp"
#include "../config/exchangeconfig.hpp"
#include "../order/order.hpp"
#include "../order/orderbook.hpp"
#include "../order/orderfactory.hpp"
#include "../trade/trade.hpp"
#include "../trade/tradefactory.hpp"
#include "../trade/marketdata.hpp"
#include "../trade/lobsnapshot.hpp"
#include "../trade/profitsnapshot.hpp"
#include "../utilities/syncqueue.hpp"
#include "../utilities/csvwriter.hpp"
#include "../utilities/csvprintable.hpp"
#include "../message/message.hpp"
#include "../message/market_data_message.hpp"
#include "../message/limit_order_message.hpp"
#include "../message/market_order_message.hpp"
#include "../message/cancel_order_message.hpp"
#include "../message/subscribe_message.hpp"
#include "../message/exec_report_message.hpp"
#include "../message/event_message.hpp"
#include "../message/cancel_reject_message.hpp"
#include "../config/simulationconfig.hpp"

class StockExchange : public Agent
{
public:

    StockExchange(NetworkEntity *network_entity, ExchangeConfigPtr config)
    : Agent(network_entity, std::static_pointer_cast<AgentConfig>(config)),
      exchange_name_{config->name},
      order_books_{},
      subscribers_{},
      trade_tapes_{},
      market_data_feeds_{},
      msg_queue_{},
      random_generator_{std::random_device{}()}
    {
      // Create message tape to log incoming messages
      createMessageTape();

      // Add all tickers to exchange
      for (auto ticker : config->tickers)
      {
        addTradeableAsset(ticker);
      }

      // Set trading window
      setTradingWindow(config->connect_time, config->trading_time);
    }

    /** Starts the exchange. */
    void start() override;

    /** Gracefully terminates the exchange, freeing all memory. */
    void terminate() override;

    /** Adds the given asset as tradeable and initialises an empty order book. */
    void addTradeableAsset(std::string_view ticker);

    /** Waits for incoming connections then opens trading window for the specified duration (seconds). */
    void setTradingWindow(int connect_time, int trading_time);

    /** Starts trading session and informs all market data subscribers. */
    void startTradingSession();

    /** Ends trading session and informs all market data subscribers. */
    void endTradingSession();

    /** Returns the pointer to the order book for the given ticker. */
    OrderBookPtr getOrderBookFor(std::string_view ticker);

    /** Returns the trade tape writer for the given ticker. */
    CSVWriterPtr getTradeTapeFor(std::string_view ticker);

    /** Returns the market data feed for the given ticker. */
    CSVWriterPtr getMarketDataFeedFor(std::string_view ticker);

    /** Returns the LOB snapshot feed for the given ticker. */
    CSVWriterPtr getLOBSnapshotFor(std::string_view ticker);

    /** Adds the given subscriber to the market data subscribers list. */
    void addSubscriber(std::string_view ticker, int subscriber_id, std::string_view address);

    /** Signal to technical indicator agents to start trading. */
    void signalTechnicalAgentsStarted(); 

private:

    /**
     *   HELPER METHODS
    */

    /** Runs the matching engine. */
    void runMatchingEngine();

    /** Checks if the given order crosses the spread. */
    bool crossesSpread(LimitOrderPtr order);

    /** Matches the given order with the orders currently present in the OrderBook.
     *  Partial execution is allowed. */
    void matchOrder(LimitOrderPtr order);

    /** Matches the given order with the orders currently present in the OrderBook. 
     *  Order must be executed in full. */
    void matchOrderInFull(LimitOrderPtr order);

    /** Cancels the given order and sends a cancellation report to the sender. */
    void cancelOrder(OrderPtr order);

    /** Executes the trade between the resting and aggressing orders. */
    void executeTrade(LimitOrderPtr resting_order, OrderPtr aggressing_order, TradePtr trade);

    /** Adds the given trade to the trade tape. */
    void addTradeToTape(TradePtr trade);

    /** Creates new trade tape and market data feed CSV files. */
    void createDataFiles(std::string_view ticker);

    /** Logs the given market data snapshot. */
    void addMarketDataSnapshot(MarketDataPtr data);

    /** Logs a snapshot of the LOB with selected attributes. */
    void addLOBSnapshot(LOBSnapshotPtr lob_data);
    
    /** Calculate profits for csv file. */
    void computeProfits();

    /** Write profits to CSV file. */
    void writeProfitsToCSV();

    /** Print profits to stock exchange terminal. */
    void printProfits();

    /** Creates a new message tape CSV file. */
    void createMessageTape();

    /** Calculates p* (p equilibrium) */
    double calculatePEquilibrium(std::string_view ticker);

    /** Calculate Smith's Alpha */
    double calculateSmithsAlpha(std::string_view ticker);

    /** Adds the given message to the message tape. */
    void addMessageToTape(MessagePtr msg);

    /**
     *   MESSAGE SENDERS
    */

    /** Sends execution report to the trader. */
    void sendExecutionReport(std::string_view trader, ExecutionReportMessagePtr msg);

    /** Publishes market data to all subscribers. */
    void publishMarketData(std::string_view ticker, Order::Side side); 

    /** Broadcasts the given message to all subscribers of the given ticker. */
    void broadcastToSubscribers(std::string_view ticker, MessagePtr msg);

    /**
     *   MESSAGE HANDLERS
    */

    /** Handles a market order message. Immediate or Cancel (IOC) orders only. */
    void onMarketOrder(MarketOrderMessagePtr msg);

    /** Handles a limit order message. */
    void onLimitOrder(LimitOrderMessagePtr msg);

    /** Handles a cancel order message. */
    void onCancelOrder(CancelOrderMessagePtr msg);

    /** Handles a subscription to market data request message. */
    void onSubscribe(SubscribeMessagePtr msg);

    /** Checks the type of the incoming message and makes a callback. */
    std::optional<MessagePtr> handleMessageFrom(std::string_view sender, MessagePtr message) override;

    /** Checks the type of the incoming broadcast and makes a callback. */
    void handleBroadcastFrom(std::string_view sender, MessagePtr message) override;

    /** Ensure directory to store data files exists. */
    void confirmDirectory(const std::string& dirPath);

    /**
     *   PRIVATE MEMBERS
    */

    /** The unique name of the exchange*/
    std::string exchange_name_;

    /** Order books for each ticker traded. */
    std::unordered_map<std::string, OrderBookPtr> order_books_;

    /** Trade tape for each ticker traded. */
    std::unordered_map<std::string, CSVWriterPtr> trade_tapes_;

    /** Market data feed snapshots for each ticker traded. */
    std::unordered_map<std::string, CSVWriterPtr> market_data_feeds_;

    /** LOB snapshot feed for each ticker traded. */
    std::unordered_map<std::string, CSVWriterPtr> lob_snapshot_;

    std::unordered_map<std::string, CSVWriterPtr> profits_writer_;

    /** Message tape for each message received. */
    CSVWriterPtr message_tape_;

    /** Subscribers for each ticker traded. */
    std::unordered_map<std::string, std::unordered_map<int, std::string>> subscribers_;

    /** Thread-safe FIFO queue for incoming messages to be processed by the matching engine. */
    SyncQueue<MessagePtr> msg_queue_;

    OrderFactory order_factory_;
    TradeFactory trade_factory_;

    /** Conditional variable signalling whether trading window is open */
    bool trading_window_open_ = false;
    std::mutex trading_window_mutex_;
    std::condition_variable trading_window_cv_;
    std::thread* trading_window_thread_ = nullptr;
    std::thread* matching_engine_thread_ = nullptr;

    /** Used for randomising the order of UDP broadcasts */
    std::mt19937 random_generator_;

    /** Add trade to in-memory list */
    std::unordered_map<std::string, std::vector<TradePtr>> in_memory_trades_;

    /** Profit attributes. */
    std::unordered_map<int, double> agent_profits_; 
    std::unordered_map<int, std::string> agent_names_;
    std::unordered_map<std::string, double> total_profits_;
    std::unordered_map<std::string, double> buyer_profits_;
    std::unordered_map<std::string, double> seller_profits_;
    std::unordered_set<int> received_profit_traders_;
    std::mutex profit_mutex_;
    std::condition_variable profit_cv_;
    size_t expected_trader_count_ = 0; 

    /** Legacy vs Technical agents. */
    bool technical_traders_ready_ = false; 
    std::unordered_set<std::string> legacy_trader_types_ = {"zic", "zip", "shvr", "deeplstm", "deepxgb"};
    std::chrono::time_point<std::chrono::high_resolution_clock> ready_timestamp_;
    const int TECHNICAL_READY_DELAY_SECONDS = 4; 

    /** Simulation config params. */
    SimulationConfigPtr simulation_config_;

    std::unordered_map<std::string, double> agent_profits_by_name_;
  
    std::chrono::high_resolution_clock::time_point trading_session_start_time_;
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> last_trade_time_;

};

#endif