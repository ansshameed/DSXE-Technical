// trader_deep_xgb.hpp
#ifndef TRADER_DEEP_XGB_HPP
#define TRADER_DEEP_XGB_HPP

#include "traderagent.hpp"
#include "../message/profitmessage.hpp"
#include "../message/customer_order_message.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "onnxruntime/onnxruntime_cxx_api.h"

using json = nlohmann::json;

class TraderDeepXGB : public TraderAgent
{
public:
    TraderDeepXGB(NetworkEntity *network_entity, TraderConfigPtr config)
    : TraderAgent(network_entity, config),
    exchange_{config->exchange_name},
    ticker_{config->ticker},
    trader_side_{config->side},
    limit_price_{config->limit}
    {
        // Create a log file
        std::ofstream init_log("./logs/deeptrader_xgb_init.log", std::ios::app);
        init_log << " TraderDeepXGB Initialisation START " << std::endl;
        init_log << "Timestamp: " << std::time(nullptr) << std::endl;
        
        // Initialise ONNX Runtime and load the model
        initialiseModel();
        
        // Test model connection
        model_initialised_ = testModelInitialisation();
        init_log << "ONNX model initialisation test: " << (model_initialised_ ? "SUCCESS" : "FAILED") << std::endl;
        
        // No delayed start for deep trader
        is_legacy_trader_ = true;
        
        // Connect to exchange
        TraderAgent::connect(config->exchange_addr, config->exchange_name, [=, this](){
            TraderAgent::subscribeToMarket(config->exchange_name, config->ticker);
        });
    
        init_log << "DeepTraderXGB initialisation complete" << std::endl;
        init_log.close();
    }

    ~TraderDeepXGB() {
    }
    
    std::string getAgentName() const override { return "DEEPXGB"; } // To determine if legacy trader
    
    void onTradingStart() override
    {
        std::cout << "Trading window started for DeepTraderXGB.\n";
        is_trading_ = true;
    }

    void onTradingEnd() override
    {
        is_trading_ = false;
        std::cout << "Trading window ended for DeepTraderXGB.\n";
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
            
            // Get predicted price using XGBoost
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
            std::cout << "DeepTraderXGB (customer): " << (side == Order::Side::BID ? "BID" : "ASK") 
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
            std::cout << "DeepTraderXGB (default): " << (trader_side_ == Order::Side::BID ? "BID" : "ASK") 
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

        std::cout << "DeepTraderXGB received execution report from " << exchange << ": Order: " << msg->order->id << " Status: " << msg->order->status << " Qty remaining = " << msg->order->remaining_quantity << "\n";
    }

    void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) override
    {
        std::cout << "DeepTraderXGB received cancel reject from " << exchange 
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
                std::cout << "[DEEPXGB] Received CUSTOMER_ORDER: side=" << (cust_msg->side == Order::Side::BID ? "BID" : "ASK") << " limit=" << cust_msg->price << "\n";
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
    
    // ONNX Runtime environment and session
    Ort::Env ort_env{ORT_LOGGING_LEVEL_WARNING, "TraderDeepXGB"};
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> ort_session;
    
    // Storage for model normalisation parameters
    std::vector<float> min_values;
    std::vector<float> max_values;
    
    void initialiseModel() {
        try {
            // Look for XGBoost model - different path from LSTM
            std::string model_path = "../src/deeptrader/dt_xgb/xgb_models/DeepTrader_XGB.onnx";
            
            // Check if ONNX model file exists
            if (!std::filesystem::exists(model_path)) {
                std::cerr << "XGBoost ONNX model file not found at: " << model_path << std::endl;
                return;
            }
            
            std::cout << "Loading XGBoost ONNX model from: " << model_path << std::endl;
            
            // Set graph optimisation level
            session_options.SetGraphOptimizationLevel(ORT_ENABLE_BASIC);
            
            // Create session
            ort_session = std::make_unique<Ort::Session>(ort_env, model_path.c_str(), session_options);
            
            // Load normalisation parameters from JSON file
            std::string norm_path = "../src/deeptrader/normalised_data/min_max_values.json";
            loadNormalisationValues(norm_path);
            
            std::cout << "XGBoost ONNX model loaded successfully" << std::endl;
        }
        catch (const Ort::Exception& e) {
            std::cerr << "ONNX Runtime error: " << e.what() << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "Error initialising model: " << e.what() << std::endl;
        }
    }
    
    void loadNormalisationValues(const std::string& file_path) {
        try {
            // If the normalisation file doesn't exist, use default values
            if (!std::filesystem::exists(file_path)) {
                std::cerr << "Normalisation file not found: " << file_path << std::endl;
                std::cerr << "Using default normalisation values" << std::endl;
                
                // Create default values
                min_values.resize(14, 0.0f);
                max_values.resize(14, 1.0f);
                return;
            }
            
            // Read the JSON file
            std::ifstream norm_file(file_path);
            json norm_data;
            norm_file >> norm_data;
            
            // Extract the min and max values
            min_values = norm_data["min_values"].get<std::vector<float>>();
            max_values = norm_data["max_values"].get<std::vector<float>>();
            
            std::cout << "Loaded normalisation values: min size=" << min_values.size() 
                      << ", max size=" << max_values.size() << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "Error loading normalisation values: " << e.what() << std::endl;
            // Set default values
            min_values.resize(14, 0.0f);
            max_values.resize(14, 1.0f);
        }
    }
    
    bool testModelInitialisation() {
        // Simple test to check if model is initialised
        return ort_session != nullptr && min_values.size() > 0 && max_values.size() > 0;
    }
    
    double predictPrice(MarketDataMessagePtr msg, Order::Side side) {
        std::string otype = (side == Order::Side::BID) ? "Bid" : "Ask";
        double best_bid = msg->data->best_bid;
        double best_ask = msg->data->best_ask;
        
        // Create log file for predictions DEBUG
        std::ofstream prediction_log("./logs/deeptrader_xgb_predictions.log", std::ios::app);
        prediction_log << "\n--- New Prediction Request ---" << std::endl;
        prediction_log << "Timestamp: " << std::time(nullptr) << std::endl;
        prediction_log << "Order Type: " << otype << std::endl;
        prediction_log << "Market Data - Best Bid: " << best_bid << ", Best Ask: " << best_ask << std::endl;
    
        // If model isn't available, use fallback
        if (!model_initialised_ || !ort_session) {
            prediction_log << "Model not initialised, using fallback price" << std::endl;
            double fallback_price = otype == "Ask" ? best_ask : best_bid;
            prediction_log << "Fallback Price: " << fallback_price << std::endl;
            prediction_log.close();
            return fallback_price;
        }
        
        try {
            // Create input feature array (13 features like in the Python version)
            std::vector<float> features = {
                static_cast<float>(msg->data->timestamp),
                static_cast<float>(msg->data->time_diff),
                side == Order::Side::BID ? 1.0f : 0.0f,
                static_cast<float>(msg->data->best_bid),
                static_cast<float>(msg->data->best_ask),
                static_cast<float>(msg->data->micro_price),
                static_cast<float>(msg->data->mid_price),
                static_cast<float>(msg->data->imbalance),
                static_cast<float>(msg->data->spread),
                static_cast<float>(msg->data->total_volume),
                static_cast<float>(msg->data->p_equilibrium),
                static_cast<float>(msg->data->smiths_alpha),
                static_cast<float>(limit_price_)
            };
            
            // Log raw features DEBUG
            prediction_log << "\nRaw Features:" << std::endl;
            prediction_log << "timestamp: " << features[0] << std::endl;
            prediction_log << "time_diff: " << features[1] << std::endl;
            prediction_log << "is_bid: " << features[2] << std::endl;
            prediction_log << "best_bid: " << features[3] << std::endl;
            prediction_log << "best_ask: " << features[4] << std::endl;
            prediction_log << "micro_price: " << features[5] << std::endl;
            prediction_log << "mid_price: " << features[6] << std::endl;
            prediction_log << "imbalance: " << features[7] << std::endl;
            prediction_log << "spread: " << features[8] << std::endl;
            prediction_log << "total_volume: " << features[9] << std::endl;
            prediction_log << "p_equilibrium: " << features[10] << std::endl;
            prediction_log << "smiths_alpha: " << features[11] << std::endl;
            prediction_log << "limit_price: " << features[12] << std::endl;
            
            // Store original features for logging
            std::vector<float> original_features = features;
            
            // Normalise features
            for (size_t i = 0; i < features.size(); i++) {
                if (i < min_values.size() && i < max_values.size()) {
                    features[i] = (features[i] - min_values[i]) / (max_values[i] - min_values[i]);
                }
            }
            
            // Log normalised features
            prediction_log << "\nNormalised Features:" << std::endl;
            for (size_t i = 0; i < features.size(); i++) {
                prediction_log << "Feature " << i << ": " << features[i] << " (original: " << original_features[i] 
                               << ", min: " << min_values[i] << ", max: " << max_values[i] << ")" << std::endl;
            }
            
            // Prepare input tensor - KEY DIFFERENCE FROM LSTM: XGBoost uses flat vectors
            std::vector<int64_t> input_shape = {1, 13}; // [batch_size, features] - no sequence dimension
            
            // Define I/O names for the model
            Ort::AllocatorWithDefaultOptions allocator;
            Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

            // Get input names
            size_t num_input_nodes = ort_session->GetInputCount();
            std::vector<std::string> input_names_str;
            std::vector<const char*> input_names;
            for (size_t i = 0; i < num_input_nodes; i++) {
                Ort::AllocatedStringPtr input_name = ort_session->GetInputNameAllocated(i, allocator);
                input_names_str.push_back(input_name.get());
                input_names.push_back(input_names_str.back().c_str());
                prediction_log << "Input " << i << " name: " << input_names[i] << std::endl; 
            }

            // Get output names
            size_t num_output_nodes = ort_session->GetOutputCount();
            std::vector<std::string> output_names_str;
            std::vector<const char*> output_names;
            for (size_t i = 0; i < num_output_nodes; i++) {
                Ort::AllocatedStringPtr output_name = ort_session->GetOutputNameAllocated(i, allocator);
                output_names_str.push_back(output_name.get());
                output_names.push_back(output_names_str.back().c_str());
                prediction_log << "Output " << i << " name: " << output_names[i] << std::endl;
            }
            
            prediction_log << "\nRunning XGBoost ONNX model inference..." << std::endl;
            
            // Create input tensor
            auto input_tensor = Ort::Value::CreateTensor<float>(
                memory_info, features.data(), features.size(), 
                input_shape.data(), input_shape.size()
            );
            
            // Run inference (prediction)
            auto output_tensors = ort_session->Run(
                Ort::RunOptions{nullptr}, 
                input_names.data(), 
                &input_tensor, 
                1, 
                output_names.data(), 
                1
            );
            
            // Get output tensor and value
            float* output_data = output_tensors[0].GetTensorMutableData<float>();
            float normalised_output = output_data[0];
            
            prediction_log << "Model output (normalised): " << normalised_output << std::endl;
            
            // Denormalise output
            float denormalised_output = 0.0f;
            if (min_values.size() > 13 && max_values.size() > 13) {
                denormalised_output = (normalised_output * (max_values[13] - min_values[13])) + min_values[13];
                prediction_log << "Denormalised output: " << denormalised_output 
                               << " (using min: " << min_values[13] << ", max: " << max_values[13] << ")" << std::endl;
            } else {
                // Fallback if normalisation values are not available
                denormalised_output = normalised_output * 200.0f; // Assuming output is in range [0,1] and price in [0,200]
                prediction_log << "Denormalised output: " << denormalised_output 
                               << " (using fallback scaling factor: 200.0)" << std::endl;
            }
            
            // Round to nearest integer
            int model_price = static_cast<int>(std::round(denormalised_output));
            prediction_log << "Rounded model price: " << model_price << std::endl;
            
            // Apply sanity checks as in the Python code
            if (model_price < 50 || model_price > 200) {
                prediction_log << "Warning: Unreasonable prediction: " << model_price << ", using fallback" << std::endl;
                if (otype == "Ask") {
                    model_price = best_ask - 1;
                    prediction_log << "Fallback to best_ask - 1: " << model_price << std::endl;
                } else {
                    model_price = best_bid + 1;
                    prediction_log << "Fallback to best_bid + 1: " << model_price << std::endl;
                }
            }
            
            prediction_log << "Final model prediction: " << model_price << " for " << otype << std::endl;
            prediction_log.close();
            
            std::cout << "XGBoost ONNX model prediction: " << model_price << " for " << otype << std::endl;
            return model_price;
        }
        catch (const Ort::Exception& e) {
            prediction_log << "ONNX Runtime error in predictPrice: " << e.what() << std::endl;
            // Fallback to simple price
            double fallback_price = otype == "Ask" ? best_ask : best_bid;
            prediction_log << "Using fallback price: " << fallback_price << std::endl;
            prediction_log.close();
            
            std::cerr << "ONNX Runtime error in predictPrice: " << e.what() << std::endl;
            return fallback_price;
        }
        catch (const std::exception& e) {
            prediction_log << "Error in predictPrice: " << e.what() << std::endl;
            // Fallback
            double fallback_price = otype == "Ask" ? best_ask : best_bid;
            prediction_log << "Using fallback price: " << fallback_price << std::endl;
            prediction_log.close();
            
            std::cerr << "Error in predictPrice: " << e.what() << std::endl;
            return fallback_price;
        }
    }
    
    std::string exchange_;
    std::string ticker_;
    Order::Side trader_side_;
    double limit_price_;
    
    bool is_trading_ = false;
    bool model_initialised_ = false;
    
    // Order management
    std::mutex mutex_;
    std::map<int, OrderInfo> orders_map_;
    std::queue<int> pending_orders_;
    int next_order_id_ = 1;

    std::stack<CustomerOrderMessagePtr> customer_orders_;
    std::mt19937 random_generator_{std::random_device{}()};
};

#endif