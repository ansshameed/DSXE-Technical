#include <algorithm>
#include <stack>
#include <filesystem> 

#include "stockexchange.hpp"
#include "../utilities/syncqueue.hpp"
#include "../trade/lobsnapshot.hpp" // Include the LOB Snapshot header file
#include "../trade/profitsnapshot.hpp" // Include the Profit Snapshot header file
#include "../message/profitmessage.hpp" // Include the Profit Message header file
#include "../config/simulationconfig.hpp" // Include the Simulation Config header file for profits

#include <iostream> // to print full profitability
#include <iomanip>
#include <chrono>

void StockExchange::start()
{ 
    // Create a Matching Engine Thread
    matching_engine_thread_ = new std::thread(&StockExchange::runMatchingEngine, this);
    
    // Main thread continues to handle incoming and outgoing communication
    Agent::start();
};

void StockExchange::terminate()
{
    if (matching_engine_thread_ != nullptr)
    {
        matching_engine_thread_->join();
        delete(matching_engine_thread_);
    }

    if (trading_window_thread_ != nullptr)
    {
        trading_window_thread_->join();
        delete(trading_window_thread_);
    }
}

void StockExchange::runMatchingEngine()
{
    // Wait until trading window opens
    std::unique_lock<std::mutex> trading_window_lock(trading_window_mutex_);
    trading_window_cv_.wait(trading_window_lock, [this]{ return trading_window_open_;});

    // Atomically check if trading window is open, and if not break loop
    while (trading_window_open_)
    {
        trading_window_lock.unlock();
        // std::cout << "Matching engine unlocked" << "\n";
        
        // Wait until new message is present
        MessagePtr msg = msg_queue_.pop();
        if (msg != nullptr)
        {
            // Pattern match the message type
            switch (msg->type) {
                case MessageType::MARKET_ORDER:
                {
                    onMarketOrder(std::dynamic_pointer_cast<MarketOrderMessage>(msg));
                    break;
                }
                case MessageType::LIMIT_ORDER:
                {
                    onLimitOrder(std::dynamic_pointer_cast<LimitOrderMessage>(msg));
                    break;
                }
                case MessageType::CANCEL_ORDER:
                {
                    onCancelOrder(std::dynamic_pointer_cast<CancelOrderMessage>(msg));
                    break;
                }
                default:
                {
                    std::cout << "Exchange received unknown message type" << "\n";
                }
            }

            msg->markProcessed();
            addMessageToTape(msg);
        }
        
        // std::cout << "Matching engine attempting to lock" << "\n";
        trading_window_lock.lock();
        // std::cout << "Matching engine locked" << "\n";
    }

    std::cout << "Matching Engine stopping." << "\n";

    trading_window_lock.unlock();
    std::cout << "Stopped running matching engine" << "\n";
    // trading_window_cv_.notify_all();
};

void StockExchange::onLimitOrder(LimitOrderMessagePtr msg)
{
    LimitOrderPtr order = order_factory_.createLimitOrder(msg);

    // Check if the incoming order crosses the spread 
    // If yes, grab the current LOB data, time etc. 
    // Build an LOB snapshot that includes chosen_price = order->price 
    // Write that snapshot to CSV 
    // Then proceed with normal matching

    // Check if it crosses the spread 
    bool crosses = crossesSpread(order); 

    // Matching 
    if (crosses)
    {
        if (order->time_in_force == Order::TimeInForce::FOK)
        {
            matchOrderInFull(order);
        }
        else
        {
            matchOrder(order);
        }
    }
    else
    {
        getOrderBookFor(order->ticker)->addOrder(order);
        ExecutionReportMessagePtr report = ExecutionReportMessage::createFromOrder(order);
        report->sender_id = this->agent_id;
        sendExecutionReport(std::to_string(order->sender_id), report);
        publishMarketData(msg->ticker, msg->side);
    }    
};

void StockExchange::onMarketOrder(MarketOrderMessagePtr msg)
{
    MarketOrderPtr order = order_factory_.createMarketOrder(msg);

    if (msg->side == Order::Side::BID)
    {
        std::optional<LimitOrderPtr> best_ask = getOrderBookFor(msg->ticker)->bestAsk();

        while (best_ask.has_value() && !order->isFilled())
        {
            getOrderBookFor(msg->ticker)->popBestAsk();

            TradePtr trade = trade_factory_.createFromLimitAndMarketOrders(best_ask.value(), order);
            addTradeToTape(trade);
            executeTrade(best_ask.value(), order, trade);

            best_ask = getOrderBookFor(order->ticker)->bestAsk();
        }
    }
    else
    {
        std::optional<LimitOrderPtr> best_bid = getOrderBookFor(msg->ticker)->bestBid();

        while (best_bid.has_value() && !order->isFilled())
        {
            getOrderBookFor(msg->ticker)->popBestBid();

            TradePtr trade = trade_factory_.createFromLimitAndMarketOrders(best_bid.value(), order);
            addTradeToTape(trade);
            executeTrade(best_bid.value(), order, trade);

            best_bid = getOrderBookFor(order->ticker)->bestBid();
        }
    }

    // If the market order is not fully executed, cancel the remaining quantity
    if (!order->isFilled())
    {
        cancelOrder(order);
    }
};

void StockExchange::onCancelOrder(CancelOrderMessagePtr msg)
{
    std::optional<LimitOrderPtr> order = getOrderBookFor(msg->ticker)->removeOrder(msg->order_id, msg->side);
    
    if (order.has_value()) 
    {
        cancelOrder(order.value());
    }
    else
    {
        // Send a cancel reject message if order does not exist in the order book
        CancelRejectMessagePtr reject = std::make_shared<CancelRejectMessage>();
        reject->sender_id = this->agent_id;
        reject->order_id = msg->order_id;

        sendMessageTo(std::to_string(msg->sender_id), std::dynamic_pointer_cast<Message>(reject), true);
    }
};

bool StockExchange::crossesSpread(LimitOrderPtr order)
{
    if (order->side == Order::Side::BID)
    {
        std::optional<LimitOrderPtr> best_ask = getOrderBookFor(order->ticker)->bestAsk();
        if (best_ask.has_value() && order->price >= best_ask.value()->price)
        {
            return true;
        }
    }
    else
    {
        std::optional<LimitOrderPtr> best_bid = getOrderBookFor(order->ticker)->bestBid();
        if (best_bid.has_value() && order->price <= best_bid.value()->price)
        {
            return true;
        }
    }
    return false;
};

void StockExchange::matchOrder(LimitOrderPtr order)
{
    if (order->side == Order::Side::BID) {
        std::optional<LimitOrderPtr> best_ask = getOrderBookFor(order->ticker)->bestAsk();
        
        while (best_ask.has_value() && !order->isFilled() && order->price >= best_ask.value()->price)
        {
            getOrderBookFor(order->ticker)->popBestAsk();

            TradePtr trade = trade_factory_.createFromLimitOrders(best_ask.value(), order);
            addTradeToTape(trade);
            executeTrade(best_ask.value(), order, trade);

            best_ask = getOrderBookFor(order->ticker)->bestAsk();
        }
    }
    else
    {
        std::optional<LimitOrderPtr> best_bid = getOrderBookFor(order->ticker)->bestBid();

        while (best_bid.has_value() && !order->isFilled() && order->price <= best_bid.value()->price)
        {
            getOrderBookFor(order->ticker)->popBestBid();

            TradePtr trade = trade_factory_.createFromLimitOrders(best_bid.value(), order);
            addTradeToTape(trade);
            executeTrade(best_bid.value(), order, trade);

            best_bid = getOrderBookFor(order->ticker)->bestBid();
        }
    }

    // If the incoming order is Good-Til-Cancelled (GTC) and not fully executed, add it to the order book
    if (!order->isFilled() && order->time_in_force == Order::TimeInForce::GTC){
        getOrderBookFor(order->ticker)->addOrder(order);
    }
    // Cancel the remainder of the order otherwise
    else if (order->time_in_force == Order::TimeInForce::IOC)
    {
        cancelOrder(order);
    }
};

void StockExchange::matchOrderInFull(LimitOrderPtr order)
{
    int temp_rem_quantity = order->remaining_quantity;
    std::stack<LimitOrderPtr> stack;

    if (order->side == Order::Side::BID) {
        std::optional<LimitOrderPtr> best_ask = getOrderBookFor(order->ticker)->bestAsk();
        while (best_ask.has_value() && temp_rem_quantity > 0 && order->price >= best_ask.value()->price)
        {
            getOrderBookFor(order->ticker)->popBestAsk();
            stack.push(best_ask.value());
            temp_rem_quantity -= std::min(temp_rem_quantity, best_ask.value()->remaining_quantity);
            best_ask = getOrderBookFor(order->ticker)->bestAsk();
        }
    }
    else
    {
        std::optional<LimitOrderPtr> best_bid = getOrderBookFor(order->ticker)->bestBid();
        while (best_bid.has_value() && temp_rem_quantity > 0 && order->price <= best_bid.value()->price)
        {
            getOrderBookFor(order->ticker)->popBestBid();

            stack.push(best_bid.value());
            temp_rem_quantity -= std::min(temp_rem_quantity, best_bid.value()->remaining_quantity);

            best_bid = getOrderBookFor(order->ticker)->bestBid();
        }
    }

    // Cancel the order if not executed in full and add all matching orders back to the order book
    if (temp_rem_quantity > 0)
    {
        // Add all matching orders back to the order book
        while (!stack.empty())
        {
            getOrderBookFor(order->ticker)->addOrder(stack.top());
            stack.pop();
        }
        
        // Cancel the incoming order
        cancelOrder(order);
    }
    // Execute the order in full
    else
    {
        while (!stack.empty())
        {
            LimitOrderPtr matched_order = stack.top();
            stack.pop();

            // Execute trade
            TradePtr trade = trade_factory_.createFromLimitOrders(matched_order, order);
            addTradeToTape(trade);
            executeTrade(matched_order, order, trade);
        }
    }
};

void StockExchange::cancelOrder(OrderPtr order)
{
    order->setStatus(Order::Status::CANCELLED);
    ExecutionReportMessagePtr report = ExecutionReportMessage::createFromOrder(order);
    report->sender_id = this->agent_id;
    sendExecutionReport(std::to_string(order->sender_id), report);
}

void StockExchange::executeTrade(LimitOrderPtr resting_order, OrderPtr aggressing_order, TradePtr trade)
{   
    // Elapsed time since trading session start in seconds. 
    auto now = std::chrono::high_resolution_clock::now();
    double elapsed_time = std::chrono::duration<double, std::milli>(now - trading_session_start_time_).count();

    // Time difference between the current trade and the last trade
    double time_diff = 0.0;
    if (last_trade_time_.find(resting_order->ticker) != last_trade_time_.end()) {
        time_diff = std::chrono::duration<double, std::milli>(now - last_trade_time_[resting_order->ticker]).count();
    } else {
        time_diff = trade->price;
    }
    // Update last trade timestamp
    last_trade_time_[resting_order->ticker] = now; 

    // Calculate trade profits
    if (aggressing_order->side == Order::Side::BID) { // Buyer = aggressor, Seller = resting order
        trade->buyer_profit = (trade->buyer_priv_value - trade->price); 
        trade->seller_profit = (trade->price - trade->seller_priv_value); 
    } else {
        trade->buyer_profit = (trade->buyer_priv_value - trade->price); 
        trade->seller_profit = (trade->price - trade->seller_priv_value);
    }

    // Calculate profits using the exact same logic as in bookkeepTrade
    double resting_profit = 0.0;
    double aggressing_profit = 0.0;

    // Cast aggressing_order to LimitOrder if needed for priv_value access
    LimitOrderPtr aggressing_limit_order = std::dynamic_pointer_cast<LimitOrder>(aggressing_order);
    
    // Calculate resting order profit
    if (resting_order->side == Order::Side::BID) {
        resting_profit = (resting_order->priv_value - trade->price);
    } else {
        resting_profit = (trade->price - resting_order->priv_value);
    }
    
    // Calculate aggressing order profit
    if (aggressing_order->side == Order::Side::BID) {
        if (aggressing_limit_order) {
            aggressing_profit = (aggressing_limit_order->priv_value - trade->price);
        } else {
            // For market orders, we use trade->buyer_priv_value as it may not have priv_value directly
            aggressing_profit = (trade->buyer_priv_value - trade->price);
        }
    } 
    else 
    {
        if (aggressing_limit_order) {
            aggressing_profit = (trade->price - aggressing_limit_order->priv_value);
        } else {
            // For market orders, we use trade->seller_priv_value
            aggressing_profit = (trade->price - trade->seller_priv_value);
        }
    }
    
    // Update profit tracking directly in the exchange
    std::string resting_name = agent_names_[resting_order->sender_id];
    std::string aggressing_name = agent_names_[aggressing_order->sender_id];
    
    // Update profits by trader name
    agent_profits_by_name_[resting_name] += resting_profit;
    agent_profits_by_name_[aggressing_name] += aggressing_profit;

    // Decrement the quantity of the orders by quantity traded
    getOrderBookFor(resting_order->ticker)->updateOrderWithTrade(resting_order, trade);
    getOrderBookFor(resting_order->ticker)->updateOrderWithTrade(aggressing_order, trade);

    // Re-insert the resting order if it has not been fully filled
    if (resting_order->remaining_quantity > 0) {
        getOrderBookFor(resting_order->ticker)->addOrder(resting_order);
    }

    // Log the trade in the order book
    getOrderBookFor(resting_order->ticker)->logTrade(trade);

    // Send execution reports to the traders
    ExecutionReportMessagePtr resting_report = ExecutionReportMessage::createFromTrade(resting_order, trade);
    resting_report->sender_id = this->agent_id;
    ExecutionReportMessagePtr aggressing_report = ExecutionReportMessage::createFromTrade(aggressing_order, trade);
    aggressing_report->sender_id = this->agent_id;
    sendExecutionReport(std::to_string(resting_order->sender_id), resting_report);
    sendExecutionReport(std::to_string(aggressing_order->sender_id), aggressing_report);

    MarketDataPtr data = getOrderBookFor(resting_order->ticker)->getLiveMarketData(aggressing_order->side);
    if (data) 
    {
        double p_equilibrium = calculatePEquilibrium(resting_order->ticker);
        double smiths_alpha = calculateSmithsAlpha(resting_order->ticker);
        int side_int = (aggressing_order->side == Order::Side::BID) ? 1 : 0;

        // Limit price from the aggressing order (the one that initiated the trade) 
        double limit_price = 0.0; 
        if (aggressing_limit_order) {
            limit_price = aggressing_limit_order->price;
        } else {
            // For market orders, we use the trade price as the limit price
            limit_price = trade->price;
        }

        LOBSnapshotPtr lob_data = std::make_shared<LOBSnapshot>( 
            data->ticker, 
            side_int, 
            static_cast<unsigned long long>(elapsed_time), 
            static_cast<unsigned long long>(time_diff),
            data->best_bid,
            data->best_ask, 
            data->micro_price,
            data->mid_price,
            data->imbalance,
            data->spread,
            data->total_volume, 
            p_equilibrium,
            smiths_alpha,
            limit_price,  // Limit price chosen for the trade
            trade->price  // Actual trade price as target variable
        ); 

        addLOBSnapshot(lob_data);
    } 

    publishMarketData(resting_order->ticker, aggressing_order->side);

}


void StockExchange::sendExecutionReport(std::string_view trader, ExecutionReportMessagePtr msg)
{
    sendMessageTo(trader, std::dynamic_pointer_cast<Message>(msg), true);
};

std::optional<MessagePtr> StockExchange::handleMessageFrom(std::string_view sender, MessagePtr message)
{
    switch (message->type)
    {
        case MessageType::SUBSCRIBE:
        { 
            SubscribeMessagePtr msg = std::dynamic_pointer_cast<SubscribeMessage>(message);
            if (msg == nullptr) {
                throw std::runtime_error("Failed to cast message to SubscribeMessage");
            }
            onSubscribe(msg);
            break;
        }
        case MessageType::EVENT:
        {
            EventMessagePtr event_msg = std::dynamic_pointer_cast<EventMessage>(message);
            if (event_msg && event_msg->event_type == EventMessage::EventType::TECHNICAL_AGENTS_STARTED) 
            {
                // When a technical agent signals that it's ready to trade,
                // broadcast this to all traders
                signalTechnicalAgentsStarted();
                return std::nullopt;
            }
            // Fall through to default case for other event types
        }
        default:
        {   
            // Send message to the matching engine
            msg_queue_.push(message);
        }
    }
    return std::nullopt;
};

void StockExchange::handleBroadcastFrom(std::string_view sender, MessagePtr message)
{
    /** TODO: Decide how to handle this more elegantly. */
    throw std::runtime_error("ExchangeAgent does not handle broadcasts");
};


void StockExchange::onSubscribe(SubscribeMessagePtr msg)
{   
    std::cout << "Subscription received: Agent " << msg->sender_id << " subscribed to " << msg->ticker << " at address " << msg->address << "\n"; // DEBUG ONLY

    agent_names_[msg->sender_id] = msg->agent_name;
    std::cout << "Agent " << msg->sender_id << " is " << msg->agent_name << "\n"; // DEBUG ONLY

    if (order_books_.contains(std::string{msg->ticker}))
    {   
        std::cout << "Subscription address: " << msg->address << " Agent ID: " << msg->sender_id << "\n";
        addSubscriber(msg->ticker, msg->sender_id, msg->address);
    }
    else
    {
        throw std::runtime_error("Failed to add subscriber: Ticker " + msg->ticker + " not found");
    }
};

void StockExchange::addSubscriber(std::string_view ticker, int subscriber_id, std::string_view address)
{
    subscribers_.at(std::string{ticker}).insert({subscriber_id, std::string{address}});

    // If trader connects after trading has started, inform the trader that trading window is open
    std::unique_lock lock {trading_window_mutex_};
    if (trading_window_open_) 
    {
        lock.unlock();

        EventMessagePtr msg = std::make_shared<EventMessage>(EventMessage::EventType::TRADING_SESSION_START); 
        sendBroadcast(address, std::dynamic_pointer_cast<Message>(msg));
    }
    else 
    {
        lock.unlock();
    }
};

void StockExchange::addTradeableAsset(std::string_view ticker)
{
    order_books_.insert({std::string{ticker}, OrderBook::create(ticker)});
    subscribers_.insert({std::string{ticker}, {}});

    createDataFiles(ticker);
    std::cout << "Added " << ticker << " as a tradeable asset" << std::endl;
};

void StockExchange::confirmDirectory(const std::string& dirPath) {
    std::filesystem::path dir(dirPath);
    std::error_code ec;
    
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::cerr << "Error creating directory " << dirPath << ": " << ec.message() << std::endl;
        } else {
            std::cout << "Created directory: " << dirPath << std::endl;
        }
    }
}

// Modify your createDataFiles method to use directories
void StockExchange::createDataFiles(std::string_view ticker)
{
    // Create base directories for different file types
    std::string lob_dir = "lob_snapshots";
    std::string trades_dir = "trades";
    std::string market_data_dir = "market_data";
    std::string profits_dir = "profits";
    
    // Ensure directories exist
    confirmDirectory(lob_dir);
    confirmDirectory(trades_dir);
    confirmDirectory(market_data_dir);
    confirmDirectory(profits_dir);

    // Get current ISO 8601 timestamp
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t), "%FT%T");
    std::string timestamp = ss.str(); 

    // Set path to CSV files with directories
    std::string suffix = std::string{exchange_name_} + "_" + std::string{ticker} + "_" + timestamp;
    std::string trades_file = trades_dir + "/" + "trades_" + suffix + ".csv";
    std::string market_data_file = market_data_dir + "/" + "data_" + suffix + ".csv";
    std::string lob_snapshot_file = lob_dir + "/" + "lob_snapshot_" + suffix + ".csv";
    std::string profits_file = profits_dir + "/" + "profits_snapshot_" + suffix + ".csv";

    // Create CSV writers
    CSVWriterPtr trade_writer = std::make_shared<CSVWriter>(trades_file);
    CSVWriterPtr market_data_writer = std::make_shared<CSVWriter>(market_data_file);
    CSVWriterPtr lob_snapshot_writer = std::make_shared<CSVWriter>(lob_snapshot_file);
    CSVWriterPtr profits_writer = std::make_shared<CSVWriter>(profits_file);

    trade_tapes_.insert({std::string{ticker}, trade_writer});
    market_data_feeds_.insert({std::string{ticker}, market_data_writer});
    lob_snapshot_.insert({std::string{ticker}, lob_snapshot_writer});
    profits_writer_.insert({std::string{ticker}, profits_writer});
    
    std::cout << "Created data files in organized directories for ticker: " << ticker << std::endl;
}

// Also modify createMessageTape method
void StockExchange::createMessageTape() 
{
    // Create messages directory
    std::string messages_dir = "messages";
    confirmDirectory(messages_dir);
    
    // Get current ISO 8601 timestamp
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t), "%FT%T");
    std::string timestamp = ss.str(); 

    // Define CSV filename with directory
    std::string suffix = std::string{exchange_name_} + "_" + timestamp;
    std::string messages_file = messages_dir + "/" + "msgs_" + suffix + ".csv";

    // Create message writer
    this->message_tape_ = std::make_shared<CSVWriter>(messages_file);
    
    std::cout << "Created message tape in organized directory" << std::endl;
}

double StockExchange::calculatePEquilibrium(std::string_view ticker)
{
    const auto& trade_tape = in_memory_trades_[std::string(ticker)]; // Get the trade tape for the given ticker

    if (trade_tape.empty()) // If no trades have been made
    {
        return 0.0; // No trades available
    }

    std::vector<double> trade_prices; // Vector to store trade prices
    for (const auto& trade : trade_tape) { // Iterate through all trades
        trade_prices.push_back(trade->price);
    }

    // Apply exponentially decreasing weight decay
    std::vector<double> weights; 
    for (size_t i = 0; i < trade_prices.size(); ++i) { 
        weights.push_back(std::pow(0.9, i)); // Exponential decay with factor 0.9
    }

    // Calculate weighted average of trade prices
    double weighted_sum = 0; 
    double weight_sum = 0; 
    for (size_t i = 0; i < trade_prices.size(); ++i) { 
        weighted_sum += trade_prices[i] * weights[i]; 
        weight_sum += weights[i]; 
    }

    // NOTE: p_equilibrium value stays same/barely changes if no new trades placed. Only changes when significant trades affect the equilibrium price. 

    return weighted_sum / weight_sum; 
}

double StockExchange::calculateSmithsAlpha(std::string_view ticker) 
{ 
    const auto& trade_tape = in_memory_trades_[std::string(ticker)]; // Get the trade tape for the given ticker

    if (trade_tape.empty()) // If no trades have been made
    {
        return 0.0; // No trades available
    }

    std::vector<double> trade_prices; // Vector to store trade prices
    for (const auto& trade : trade_tape) { // Iterate through all trades
        trade_prices.push_back(trade->price);
    }

    // Calculate p_equilibrium
    double p_equilibrium = calculatePEquilibrium(ticker);

    // Compute Smith's Alpha
    double sum_squared_diff = 0; 
    for (const auto& price : trade_prices) { // Iterate through all trade prices
        sum_squared_diff += std::pow(price - p_equilibrium, 2); // Calculate sum of squared differences = (price - p_equilibrium)^2
    }
    double smiths_alpha = std::sqrt(sum_squared_diff / trade_prices.size()); // Calculate square root of the average squared difference (divide by number of trades and then sqrt)

    return smiths_alpha;
}

void StockExchange::publishMarketData(std::string_view ticker, Order::Side aggressing_side) 
{
    MarketDataPtr data = getOrderBookFor(ticker)->getLiveMarketData(aggressing_side); // Get live market data for the given ticker
    if (!data) { // DEBUG  
        std::cout << "No market data available for " << ticker << "\n";
        return;
    }

    // Always ensure the timestamps are proper millisecond values relative to session start
    auto now = std::chrono::high_resolution_clock::now();
    double elapsed_time = std::chrono::duration<double, std::milli>(now - trading_session_start_time_).count();
    
    // Time difference between current event and last trade for this ticker
    double time_diff = 0.0;
    if (last_trade_time_.find(std::string(ticker)) != last_trade_time_.end()) {
        time_diff = std::chrono::duration<double, std::milli>(now - last_trade_time_[std::string(ticker)]).count();
    }
    
    // Ensure timestamps are always properly set
    data->timestamp = static_cast<unsigned long long>(elapsed_time);
    data->time_diff = static_cast<unsigned long long>(time_diff);
    
    // Update other derived values
    data->p_equilibrium = calculatePEquilibrium(ticker);
    data->smiths_alpha = calculateSmithsAlpha(ticker);
    
    addMarketDataSnapshot(data); // Existing market data snapshot (data_ files)
    MarketDataMessagePtr msg = std::make_shared<MarketDataMessage>();
    msg->data = data;

    // Send message to all subscribers of the given ticker 
    broadcastToSubscribers(ticker, std::dynamic_pointer_cast<Message>(msg));
}; 

void StockExchange::setTradingWindow(int connect_time, int trading_time)
{
    if (trading_window_thread_ != nullptr)
    {
        throw new std::runtime_error("Trading window already has been set");
    }

    trading_window_thread_ = new std::thread([=, this](){

        // Allow time for connections
        std::cout << "Trading time set to " << trading_time << " seconds." << std::endl;
        std::cout << "Waiting for connections for " << connect_time << " seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(connect_time));

        // After the initial period, continue checking for additional connections. 
        std::cout << "Initial connection period complete. Monitoring for additional connections..." << std::endl;
        auto last_connection_time = std::chrono::steady_clock::now();
        size_t prev_count = agent_names_.size();
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));  // Check periodically
            size_t current_count = agent_names_.size();
            if (current_count > prev_count) {
                std::cout << "New connection detected. Total connected agents: " << current_count << std::endl;
                last_connection_time = std::chrono::steady_clock::now();
                prev_count = current_count;
            }
            // If no new connection for 5 seconds, then proceed.
            if (std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - last_connection_time).count() >= 5) {
                std::cout << "No new connections for 5 seconds. Proceeding to order injection phase." << std::endl;
                break;
            }
        }

        // **Phase 1: Order Injection**
        //std::cout << "Starting order injection phase before trading begins...\n";
        //EventMessagePtr order_inject_start_msg = std::make_shared<EventMessage>(EventMessage::EventType::ORDER_INJECTION_START);
        //for (const auto& [ticker, ticker_subscribers] : subscribers_) {
            //broadcastToSubscribers(ticker, std::dynamic_pointer_cast<Message>(order_inject_start_msg));
        //}
        
        //std::this_thread::sleep_for(std::chrono::seconds(5));  // Allow OrderInjector to inject orders for 3 seconds

        // **Phase 2: Start Trading Session**
        std::cout << "Order injection complete. Starting trading session now.\n";
        startTradingSession();
        std::this_thread::sleep_for(std::chrono::seconds(trading_time));

        // **Phase 3: End Trading Session**
        endTradingSession();
        std::cout << "Trading session ended.\n";
    });
}


void StockExchange::startTradingSession()
{   
    trading_session_start_time_ = std::chrono::high_resolution_clock::now();

    // Schedule the technical traders ready event
    auto ready_thread = new std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::seconds(TECHNICAL_READY_DELAY_SECONDS));
        
        // Set technical traders as ready and reset legacy trader profits
        technical_traders_ready_ = true;
        ready_timestamp_ = std::chrono::high_resolution_clock::now();
        
        // Reset profits for legacy traders
        int reset_count = 0;
        for (auto& [name, profit] : agent_profits_by_name_) {
            for (const auto& legacy_type : legacy_trader_types_) {
                if (name.find(legacy_type) == 0) {  // Name starts with legacy type
                    profit = 0.0;  // Reset profit
                    reset_count++;
                    break;
                }
            }
        }
        
        // Signal all traders that technical agents are ready
        signalTechnicalAgentsStarted();
    });
    ready_thread->detach();

    // Signal start of trading window to the matching engine
    std::unique_lock<std::mutex> trading_window_lock(trading_window_mutex_);
    trading_window_open_ = true;
    trading_window_lock.unlock();
    trading_window_cv_.notify_all();

    EventMessagePtr msg = std::make_shared<EventMessage>(EventMessage::EventType::TRADING_SESSION_START); 
    // Send a message to subscribers of all tickers
    for (auto const& [ticker, ticker_subscribers] : subscribers_)
    {
        broadcastToSubscribers(ticker, std::dynamic_pointer_cast<Message>(msg));
    }
};

void StockExchange::endTradingSession()
{
    // Signal end of trading window to the matching engine
    std::unique_lock<std::mutex> trading_window_lock(trading_window_mutex_);
    trading_window_open_ = false;
    trading_window_lock.unlock();
    trading_window_cv_.notify_all();

    // First close the message queue to prevent new trades
    msg_queue_.close();
    
    // Wait for the matching engine to stop
    if (matching_engine_thread_ != nullptr && matching_engine_thread_->joinable()) {
        matching_engine_thread_->join();
        delete matching_engine_thread_;
        matching_engine_thread_ = nullptr;
    }

    EventMessagePtr msg = std::make_shared<EventMessage>(EventMessage::EventType::TRADING_SESSION_END);
    // Send a message to subscribers of all tickers
    for (auto const& [ticker, ticker_subscribers] : subscribers_)
    {
        broadcastToSubscribers(ticker, std::dynamic_pointer_cast<Message>(msg));
    }

    // Now calculate profits and write to CSV
    std::cout << "Profits calculated internally by exchange:\n";
    for (const auto& [agentName, profit] : agent_profits_by_name_) {
        std::cout << agentName << ": " << profit << std::endl;
    }

    // Write profits to CSV
    writeProfitsToCSV();

    // Iterate through all tickers and close all open csv files
    for (auto const& [ticker, writer] : trade_tapes_)
    {
        writer->stop();
    }

    std::cout << "Trading session ended.\n";
}

void StockExchange::writeProfitsToCSV()
{
    // Ensure profits to write.
    if (agent_profits_by_name_.empty()) {
        std::cout << "ERROR: No profits to write to CSV!\n";
        return;
    }

    // Sort profits in descending order
    std::vector<std::pair<std::string, double>> sorted_profits(
        agent_profits_by_name_.begin(), agent_profits_by_name_.end());

    std::sort(sorted_profits.begin(), sorted_profits.end(), 
        [](const auto& a, const auto& b) { return a.second > b.second; }); 
    
    // Write to each ticker's profit file
    for (const auto& [ticker, writer] : profits_writer_) {
        std::cout << "Writing profits for ticker: " << ticker << "\n";
        
        for (const auto& [agentName, profit] : sorted_profits) {
            ProfitSnapshotPtr snapshot = std::make_shared<ProfitSnapshot>(agentName, profit);
            
            if (!writer) {
                std::cerr << "ERROR: Null writer for ticker " << ticker << "\n";
                continue;
            }
            
            writer->writeRow(snapshot);
            std::cout << "Wrote profit for " << agentName << ": " << profit << "\n";
        }
        
        writer->stop();
    }

    std::cout << "Finished writing profits to CSV\n";
}

void StockExchange::signalTechnicalAgentsStarted()
{
    EventMessagePtr msg = std::make_shared<EventMessage>();
    msg->event_type = EventMessage::EventType::TECHNICAL_AGENTS_STARTED;
    
    // Signal to all tickers
    for (auto const& [ticker, ticker_subscribers] : subscribers_)
    {
        broadcastToSubscribers(ticker, std::dynamic_pointer_cast<Message>(msg));
    }
}

OrderBookPtr StockExchange::getOrderBookFor(std::string_view ticker)
{
    return order_books_.at(std::string{ticker});
};

CSVWriterPtr StockExchange::getTradeTapeFor(std::string_view ticker)
{
    return trade_tapes_.at(std::string{ticker});
};

CSVWriterPtr StockExchange::getMarketDataFeedFor(std::string_view ticker)
{
    return market_data_feeds_.at(std::string{ticker});
};

CSVWriterPtr StockExchange::getLOBSnapshotFor(std::string_view ticker)
{
    return lob_snapshot_.at(std::string{ticker});
};

void StockExchange::addTradeToTape(TradePtr trade)
{
    std::cout << *trade << "\n";
    getTradeTapeFor(trade->ticker)->writeRow(trade);

    // Add trade to in-memory list 
    in_memory_trades_[trade->ticker].push_back(trade); // CORRECTLY GETTING TRADES
};

void StockExchange::addMarketDataSnapshot(MarketDataPtr data)
{
    getMarketDataFeedFor(data->ticker)->writeRow(data);
}

// Add LOB Snapshot to the LOB Snapshot file (selected attributes from data) 
void StockExchange::addLOBSnapshot(LOBSnapshotPtr lob_data)
{   
    // Write the LOB Snapshot to the LOB Snapshot file (CSV)
    getLOBSnapshotFor(lob_data->ticker)->writeRow(lob_data);
}

void StockExchange::addMessageToTape(MessagePtr msg)
{
    message_tape_->writeRow(msg);
}


void StockExchange::broadcastToSubscribers(std::string_view ticker, MessagePtr msg)
{
    // Randomise the subscribers list
    std::unordered_map<int, std::string> ticker_subcribers(subscribers_.at(std::string{ticker}));
    std::vector<std::pair<int, std::string>> randomised_subscribers(ticker_subcribers.begin(), ticker_subcribers.end());
    std::shuffle(randomised_subscribers.begin(), randomised_subscribers.end(), random_generator_);

    // Send a broadcast to each one
    for (auto const& [subscriber_id, address] : randomised_subscribers)
    {
        sendBroadcast(address, std::dynamic_pointer_cast<Message>(msg));
    }
}
