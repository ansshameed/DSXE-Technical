#include <iostream>
#include <optional>
#include <random>

#include "traderagent.hpp"

void TraderAgent::terminate() 
{
    // Only safe after delay is finished
    if (delay_thread_ != nullptr) 
    {
        delay_thread_->join();
        delete(delay_thread_);
    }
}

std::optional<MessagePtr> TraderAgent::handleMessageFrom(std::string_view sender, MessagePtr message)
{
    // If trading window (for this trader) not yet open ignore message
    std::unique_lock lock{mutex_};
    if (!trading_window_open_) return std::nullopt;
    lock.unlock();

    switch (message->type)
    {
        case MessageType::EXECUTION_REPORT:
        {
            ExecutionReportMessagePtr msg = std::dynamic_pointer_cast<ExecutionReportMessage>(message);
            if (msg == nullptr) {
                throw std::runtime_error("Failed to cast message to ExecutionReportMessage");
            }
            onExecutionReport(sender, msg);
            break;
        }
        case MessageType::CANCEL_REJECT:
        {
            CancelRejectMessagePtr msg = std::dynamic_pointer_cast<CancelRejectMessage>(message);
            if (msg == nullptr) {
                throw std::runtime_error("Failed to cast message to CancelRejectMessage");
            }
            onCancelReject(sender, msg);
            break;
        }
        default:
        {
            std::cout << "Unknown message type" << "\n";
            break;
        }
    }
    
    return std::nullopt;
}

void TraderAgent::handleBroadcastFrom(std::string_view sender, MessagePtr message)
{
    switch (message->type)
    {
        case MessageType::MARKET_DATA: 
        {
            // If trading window (for this trader) not yet open ignore message
            std::unique_lock lock{mutex_};
            if (!trading_window_open_) return;
            lock.unlock();

            MarketDataMessagePtr msg = std::dynamic_pointer_cast<MarketDataMessage>(message);
            if (msg == nullptr) {
                throw std::runtime_error("Failed to cast message to MarketDataMessage");
            }
            onMarketData(sender, msg);
            break;
        }
        case MessageType::EVENT:
        {
            EventMessagePtr msg = std::dynamic_pointer_cast<EventMessage>(message);
            if (msg == nullptr) {
                throw std::runtime_error("Failed to cast message to EventMessage");
            }

            if (msg->event_type == EventMessage::EventType::TRADING_SESSION_START)
            {
                signalTradingStart();
            }
            else if (msg->event_type == EventMessage::EventType::TRADING_SESSION_END)
            {
                onTradingEnd();
            }
            break;
        }
        default:
        {
            std::cout << "Unknown message type" << "\n";
            break;
        }
    }
}

std::string TraderAgent::getAgentName() const { return agent_name_; }

void TraderAgent::subscribeToMarket(std::string_view exchange, std::string_view ticker)
{
    SubscribeMessagePtr msg = std::make_shared<SubscribeMessage>();
    msg->ticker = std::string{ticker};
    msg->address = myAddr() + std::string{":"} + std::to_string(myPort());
    msg->agent_name = getAgentName();

    Agent::sendMessageTo(exchange, std::dynamic_pointer_cast<Message>(msg));
}

// Random order size
int TraderAgent::getRandomOrderSize() 
{ 
    int base_quantity = 50; // Min order size
    int max_quantity = 500; // Max order size

    // Create random generator 
    static std::random_device rd; 
    static std::mt19937 gen(rd());

    // Uniform distribution
std::uniform_int_distribution<> dist(base_quantity, max_quantity);

    return dist(gen); 
}

void TraderAgent::placeLimitOrder(std::string_view exchange, Order::Side side, std::string_view ticker, int quantity, double price, double priv_value, Order::TimeInForce time_in_force, int client_order_id)
{
    LimitOrderMessagePtr msg = std::make_shared<LimitOrderMessage>();
    msg->client_order_id = client_order_id;
    msg->ticker = std::string{ticker};;
    msg->quantity = quantity;
    msg->price = price;
    msg->side = side;
    msg->priv_value = priv_value;
    msg->time_in_force = time_in_force;
    msg->agent_name = getAgentName();

    Agent::sendMessageTo(exchange, std::dynamic_pointer_cast<Message>(msg));
}

void TraderAgent::placeMarketOrder(std::string_view exchange, Order::Side side, std::string_view ticker, int quantity, double priv_value)
{
    MarketOrderMessagePtr msg = std::make_shared<MarketOrderMessage>();
    msg->ticker = std::string{ticker};
    msg->quantity = quantity;
    msg->side = side;
    msg->priv_value = priv_value;
    msg->agent_name = getAgentName(); 

    Agent::sendMessageTo(exchange, std::dynamic_pointer_cast<Message>(msg));
}

void TraderAgent::cancelOrder(std::string_view exchange, Order::Side side, std::string_view ticker, int order_id)
{
    CancelOrderMessagePtr msg = std::make_shared<CancelOrderMessage>();
    msg->order_id = order_id;
    msg->ticker = std::string{ticker};
    msg->side = side;
    msg->agent_name = getAgentName(); 

    Agent::sendMessageTo(exchange, std::dynamic_pointer_cast<Message>(msg));
}

void TraderAgent::addDelayedStart(int delay_in_seconds)
{
    start_delay_in_seconds_ = delay_in_seconds;
}

void TraderAgent::signalTradingStart()
{
    delay_thread_ = new std::thread([&](){
        if (start_delay_in_seconds_ > 0) 
        {
            std::cout << "Delayed trader start: waiting " << start_delay_in_seconds_ << " to start...\n";
            std::this_thread::sleep_for(std::chrono::seconds(start_delay_in_seconds_));
        }

        std::cout << "Trader starts now.\n";
        onTradingStart();

        std::unique_lock lock{mutex_};
        trading_window_open_ = true;
        lock.unlock();
    });
}

/** Blotter to track individual trader profits. */
void TraderAgent::bookkeepTrade(const TradePtr & trade, const LimitOrderPtr & order) { 
    
    //double current_time - IF WE WANT TO RECORD PROFIT PER TIME - LOOK BELOW. PUT IN DECLARATION ABOVE IF TIME NEEDED
    double profit = 0.0; 

    if (order->side == Order::Side::BID) {
        profit = order->price - trade->price; 
    } else {
        profit = trade->price - order->price;
    }

    if (profit < 0) { 
        std::cerr << "Negative profit detected: " << profit << "\n"; 
        std::cerr << "Aborting trade to prevent loss.\n"; 
        profit = 0; 
    }

    balance += profit; 
    n_trades++; 
    //profit_per_time = balance / (current_time - birth_time_); 

    blotter_.push_back(trade);
    std::cout << "Trade booked: quantity: " << trade->quantity << " @ price: " << trade->price << " for profit: " << profit << "\n";
    std::cout << "Order price: " << order->price << ", Trade price: " << trade->price << "\n";

}