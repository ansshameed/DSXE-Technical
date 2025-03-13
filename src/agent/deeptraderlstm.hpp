// trader_deep.hpp
#ifndef TRADER_DEEP_HPP
#define TRADER_DEEP_HPP

#include "traderagent.hpp"
#include "../message/profitmessage.hpp"
#include "../message/customer_order_message.hpp"
#include <cmath>
#include <filesystem>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json; // JSON application for C++ 

class TraderDeepLSTM : public TraderAgent
{
public:
    TraderDeepLSTM(NetworkEntity *network_entity, TraderConfigPtr config)
    : TraderAgent(network_entity, config),
    exchange_{config->exchange_name},
    ticker_{config->ticker},
    trader_side_{config->side},
    limit_price_{config->limit},
    python_server_host_{"127.0.0.1"}, // Python end point for model prediction
    python_server_port_{8777}
    {
        // Create a log file
        std::ofstream init_log("./logs/deeptrader_init.log", std::ios::app);
        init_log << "--- TraderDeepLSTM Initialisation START ---" << std::endl;
        init_log << "Timestamp: " << std::time(nullptr) << std::endl;
        
        // Start Python server if not already running - should be ran before exchange
        if (!isServerRunning()) {
            startPythonServer();
            // Wait for server to start
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        
        // Test connection to Python server
        py_initialized_ = testServerConnection();
        init_log << "Python server connection test: " << (py_initialized_ ? "SUCCESS" : "FAILED") << std::endl;
        
        // No delayed start for deep trader
        is_legacy_trader_ = true;
        
        // Connect to exchange
        TraderAgent::connect(config->exchange_addr, config->exchange_name, [=, this](){
            TraderAgent::subscribeToMarket(config->exchange_name, config->ticker);
        });
    
        init_log << "DeepTrader initialisation complete" << std::endl;
        init_log.close();
    }

    ~TraderDeepLSTM() {
        // Clean up server process if needed
        if (server_pid_ > 0) {
            kill(server_pid_, SIGTERM);
        }
    }
    
    std::string getAgentName() const override { return "DEEPLSTM"; }
    
    void onTradingStart() override
    {
        std::cout << "Trading window started for DeepTrader.\n";
        is_trading_ = true;
    }

    void onTradingEnd() override
    {
        is_trading_ = false;
        std::cout << "Trading window ended for DeepTrader.\n";
    }

    void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) override
    {

        if (!is_trading_) {
            return;
        }
        
        // Generate random quantity for orders (similar to TraderShaver)
        std::uniform_int_distribution<int> dist(10, 50);
        int quantity = dist(random_generator_);
        
        // If we have customer orders, use those; otherwise use default values
        if (!customer_orders_.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto cust_order = customer_orders_.top();
            customer_orders_.pop();
            
            // Create an order based on the customer order
            Order::Side side = cust_order->side;
            limit_price_ = cust_order->price;
            
            // Get predicted price using LSTM
            double model_price = predictPrice(msg, side);
            
            // Apply price adjustments
            if (side == Order::Side::ASK) {
                if (model_price < limit_price_) {
                    model_price = limit_price_ + 1;
                    if (msg->data->best_ask > 0 && limit_price_ < msg->data->best_ask - 1) {
                        model_price = msg->data->best_ask - 1;
                    }
                }
            } else { // BID
                if (model_price > limit_price_) {
                    model_price = limit_price_ - 1;
                    if (msg->data->best_bid > 0 && limit_price_ > msg->data->best_bid + 1) {
                        model_price = msg->data->best_bid + 1;
                    }
                }
            }
            
            // Place the order
            placeLimitOrder(exchange_, side, ticker_, cust_order->quantity, model_price, limit_price_);
            std::cout << "DeepTrader (customer): " << (side == Order::Side::BID ? "BID" : "ASK") 
                      << " " << cust_order->quantity << " @ " << model_price << " (limit: " << limit_price_ << ")\n";
        } 
        else {
            // No customer orders, use default settings similar to TraderShaver
            double model_price = predictPrice(msg, trader_side_);
            
            // Apply price adjustments
            if (trader_side_ == Order::Side::ASK) {
                if (model_price < limit_price_) {
                    model_price = limit_price_ + 1;
                    if (msg->data->best_ask > 0 && limit_price_ < msg->data->best_ask - 1) {
                        model_price = msg->data->best_ask - 1;
                    }
                }
            } else { // BID
                if (model_price > limit_price_) {
                    model_price = limit_price_ - 1;
                    if (msg->data->best_bid > 0 && limit_price_ > msg->data->best_bid + 1) {
                        model_price = msg->data->best_bid + 1;
                    }
                }
            }
            
            // Place the order
            placeLimitOrder(exchange_, trader_side_, ticker_, quantity, model_price, limit_price_);
            std::cout << "DeepTrader (default): " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") 
                      << " " << quantity << " @ " << model_price << " (limit: " << limit_price_ << ")\n";
        }
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override
    {
        if (msg->trade) {
            LimitOrderPtr limit_order = std::dynamic_pointer_cast<LimitOrder>(msg->order);
            if (!limit_order) {
                throw std::runtime_error("Failed to cast order to LimitOrder.");
            }
            bookkeepTrade(msg->trade, limit_order);
        }

        std::cout << "DeepTrader received execution report from " << exchange << ": Order: " << msg->order->id << " Status: " << msg->order->status << " Qty remaining = " << msg->order->remaining_quantity << "\n";
    }

    void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) override
    {
        std::cout << "DeepTrader received cancel reject from " << exchange 
                  << " for order ID " << msg->order_id << "\n";
    }

    void handleBroadcastFrom(std::string_view sender, MessagePtr message) override
    {
        if (message->type == MessageType::CUSTOMER_ORDER) 
        {
            auto cust_msg = std::dynamic_pointer_cast<CustomerOrderMessage>(message);
            if (cust_msg) 
            { 
                std::lock_guard<std::mutex> lock(mutex_);
                // Use a stack like TraderShaver
                customer_orders_.push(cust_msg);
                std::cout << "[DEEP] Received CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
            }
            return;
        }
        
        // Handle other broadcast messages
        TraderAgent::handleBroadcastFrom(sender, message);
    }
    
private:

    // OrderInfo structure specific to Deep Trader
    struct OrderInfo {
        double price;
        int qty;
        std::string otype;
    };

    // Failover mechanism to check if Python server is running by establishing TCP connection to host and port (returns true if server is running)
    bool isServerRunning() {
        
        // Create new TCP socket and return false if socket creation fails
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;
        
        // Initialise socket address structure with target Python server's IP address adn port number; preparing for connection attempt
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(python_server_port_);
        inet_pton(AF_INET, python_server_host_.c_str(), &serv_addr.sin_addr);
        
        // Attempt to connect to the Python server and return true if connection is successful
        bool running = (::connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) >= 0);
        close(sock);
        return running;
    }
    
    // Starts Python server by forking a new process and executing the Python script (replaces current program with Python interpreter running deep_trader_server.py)
    // Parent process continues execution while child process executes the Python script
    void startPythonServer() {
        server_pid_ = fork();
        if (server_pid_ == 0) {
            // Child process
            execl("/usr/bin/python3", "python3", "../src/deeptrader/deep_trader_server.py", NULL);
            exit(1); // If execl fails
        }
        // Parent process continues
    }
    
    // Tests connection to Python server by creating a new TCP socket and attempting to connect to the server
    bool testServerConnection() {

        // Try to connect to server
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;
        
        // Prepare socket address structure
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(python_server_port_);
        inet_pton(AF_INET, python_server_host_.c_str(), &serv_addr.sin_addr);

        // Return false if connection fails
        if (::connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            close(sock);
            return false;
        }
        
        // Close socket and return true if connection is successful
        close(sock);
        return true;
    }
    
    // Predicts the price using the LSTM model by sending a JSON request to the Python server and receiving a JSON response of the predicteed price (then adjusted based on limit price constraints)
    double predictPrice(MarketDataMessagePtr msg, Order::Side side) {

        std::string otype = (side == Order::Side::BID) ? "Bid" : "Ask";
        double best_bid = msg->data->best_bid;
        double best_ask = msg->data->best_ask;

        // If Python server isn't available, use fallback
        if (!py_initialized_) {
            return otype == "Ask" ? best_ask : best_bid;
        }
        
        try {
            // Connect to Python server
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                throw std::runtime_error("Socket creation failed");
            }
            
            struct sockaddr_in serv_addr;
            memset(&serv_addr, 0, sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(python_server_port_);
            inet_pton(AF_INET, python_server_host_.c_str(), &serv_addr.sin_addr);
            
            if (::connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                close(sock);
                throw std::runtime_error("Connection to Python server failed");
            }
            
            // Prepare JSON request
            json request = {
                {"type", "predict"},
                {"timestamp", msg->data->timestamp},
                {"time_diff", msg->data->time_diff},
                {"side", (otype == "Bid" ? 1 : 0)},
                {"best_bid", msg->data->best_bid},
                {"best_ask", msg->data->best_ask},
                {"micro_price", msg->data->micro_price},
                {"mid_price", msg->data->mid_price},
                {"imbalance", msg->data->imbalance},
                {"spread", msg->data->spread},
                {"total_volume", msg->data->total_volume},
                {"p_equilibrium", msg->data->p_equilibrium},
                {"smiths_alpha", msg->data->smiths_alpha},
                {"limit_price", limit_price_}  // Add the limit price
            };
            
            std::string request_str = request.dump();
            send(sock, request_str.c_str(), request_str.size(), 0);
            
            // Receive response
            char buffer[4096] = {0};
            int valread = read(sock, buffer, 4096);
            
            close(sock);
            
            if (valread <= 0) {
                throw std::runtime_error("Failed to read from server");
            }
            
            // Parse response
            json response = json::parse(buffer);
            if (response["status"] == "success") {
                return response["price"];
            } 
            else {
                std::cerr << "Prediction error: " << response["error"] << std::endl;
                // Fallback
                return otype == "Ask" ? best_ask : best_bid;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error in predictPrice: " << e.what() << std::endl;
            // Fallback
            return otype == "Ask" ? best_ask : best_bid;
        }
    }
    
    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    std::string python_server_host_;
    int python_server_port_;
    pid_t server_pid_ = -1;
    
    bool is_trading_ = false;
    bool py_initialized_ = false;
    
    // Order management
    std::mutex mutex_;
    std::map<int, OrderInfo> orders_map_;
    std::queue<int> pending_orders_;
    int next_order_id_ = 1;

    std::stack<CustomerOrderMessagePtr> customer_orders_;
    std::mt19937 random_generator_;
};

#endif