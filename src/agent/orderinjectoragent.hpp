#ifndef ORDER_INJECTOR_AGENT_HPP
#define ORDER_INJECTOR_AGENT_HPP

#include "agent.hpp"
#include "../config/simulationconfig.hpp"
#include "../config/orderinjectorconfig.hpp"
#include "../message/customer_order_message.hpp"
#include "../message/trader_list_message.hpp"
#include "../message/request_trader_list_message.hpp"
#include "../message/event_message.hpp"
#include "traderagent.hpp"
#include <random>
#include <thread>
#include <mutex>
#include <optional>
#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <condition_variable>
#include <cmath> 


class OrderInjectorAgent : public Agent 
{
public:
    OrderInjectorAgent(NetworkEntity* network_entity, OrderInjectorConfigPtr config)
        : Agent(network_entity, config), 
          exchange_{config->exchange_name},  // Store exchange name
          ticker_{config->ticker},           // Store ticker symbol
          injection_thread_(nullptr), 
          is_injecting_(false),
          random_generator_(std::random_device{}()), 
          config_(config)
        {

        // Automatically connect to exchange on initialisations
        connect(config->exchange_addr, config->exchange_name, [=, this](){
            std::cout << "Successfully connected to exchange: " 
              << config->exchange_name 
              << " for ticker " << config->ticker << std::endl;
            subscribeToMarket(config->exchange_name, config->ticker);
        });
    }   

    /** Subscribe to market data from the given market. */
    void subscribeToMarket(std::string_view exchange, std::string_view ticker)
    {
        SubscribeMessagePtr msg = std::make_shared<SubscribeMessage>();
        msg->ticker = std::string{ticker};
        msg->address = myAddr() + std::string{":"} + std::to_string(myPort());

        std::cout << "Subscribing to market data for " << ticker << " from " << exchange << std::endl; 

        Agent::sendMessageTo(exchange, std::dynamic_pointer_cast<Message>(msg));
    }

    // Gracefully terminates the agent.
    void terminate() override {

        if (injection_thread_ != nullptr)
        {
            injection_thread_->join();
            delete(injection_thread_);
        }
    }

    void handleBroadcastFrom(std::string_view sender, MessagePtr message) override {
        if (message->type == MessageType::EVENT) {
            EventMessagePtr eventMsg = std::dynamic_pointer_cast<EventMessage>(message);
            if (!eventMsg) {
                throw std::runtime_error("Failed to cast message to EventMessage");
            }
            if (eventMsg->event_type == EventMessage::EventType::TRADING_SESSION_END) {
                std::cout << "[OrderInjector] Received TRADING_SESSION_END event. Stopping order injection.\n";
                stopInjecting();
            }
            if (eventMsg->event_type == EventMessage::EventType::TRADING_SESSION_START) {
                std::cout << "[OrderInjector] Received TRADING_SESSION_START event. Starting order injection.\n";
                startInjecting();
            }
        }
        // Optionally, ignore other message types silently or handle them as needed.
    }
    

    std::optional<MessagePtr> handleMessageFrom(std::string_view sender, MessagePtr message) override
    {
        if (message->type == MessageType::TRADER_LIST_RESPONSE) {
            TraderListMessagePtr trader_list_msg = std::dynamic_pointer_cast<TraderListMessage>(message);
            if (!trader_list_msg) {
                throw std::runtime_error("Failed to cast message to TraderListMessage.");
            }

            // Avoid overwriting trader addresses if they have already been set
            if (!trader_addresses_.empty()) {
                std::cout << "[OrderInjector] Warning: Trader addresses already received, ignoring duplicate response.\n";
                return std::nullopt;
            }

            trader_addresses_ = trader_list_msg->trader_addresses;
            std::cout << "[OrderInjector] Received trader addresses from Orchestrator.\n";

            if (trader_addresses_.empty()) {
                std::cerr << "[OrderInjector] Warning: No traders available for order injection.\n";
            }
        }

        if (message->type == MessageType::EVENT) {
            EventMessagePtr eventMsg = std::dynamic_pointer_cast<EventMessage>(message);
            if (!eventMsg) {
                throw std::runtime_error("Failed to cast message to EventMessage");
            }

            if (eventMsg->event_type == EventMessage::EventType::ORDER_INJECTION_START) {
                std::cout << "[OrderInjector] Received ORDER_INJECTION_START event. Beginning order injection.\n";
                startInjecting();
            } 
            if (eventMsg->event_type == EventMessage::EventType::ORDER_INJECTION_STOP) {
                std::cout << "[OrderInjector] Received ORDER_INJECTION_STOP event. Stopping order injection.\n";
                stopInjecting();
            }
        }

        return std::nullopt;
    }

private:

    std::string exchange_;
    std::string ticker_;
    OrderInjectorConfigPtr config_;
    std::thread* injection_thread_;
    std::mutex mutex_;
    bool is_injecting_;
    std::mt19937 random_generator_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
    std::vector<std::string> trader_addresses_;
    int next_client_order_id_;

    void startInjecting() {
        std::cout << "[OrderInjector] Starting order injection now.\n";

        if (trader_addresses_.empty()) {
            std::cerr << "[OrderInjector] No traders to send orders to. Aborting injection.\n";
            return;
        }

        is_injecting_ = true;
        start_time_ = std::chrono::high_resolution_clock::now();
        injection_thread_ = new std::thread([this]() { injectOrders(); });
    }

    void stopInjecting() 
    {
        std::unique_lock<std::mutex> lock(mutex_);
        std::cout << "Trading window ended.\n";
        is_injecting_ = false;
        lock.unlock();
        if (injection_thread_ != nullptr) {
            if (injection_thread_->joinable()) {
                injection_thread_->join();
            }
            delete injection_thread_;
            injection_thread_ = nullptr;
        }
    }
    

    double getElapsedTime() {
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time_).count();
        std::cout << "[OrderInjector] Elapsed Time: " << elapsed << "s\n";
        return elapsed;
    }
    
    // Parses a time string "HH:MM:SS" into seconds since midnight.
    static double parseTime(const std::string &time_string) {
        std::tm tm = {};
        std::istringstream ss(time_string);
        ss >> std::get_time(&tm, "%H:%M:%S");
        if (ss.fail()) {
            throw std::invalid_argument("Invalid time format: " + time_string);
        }
        return tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    }

    // Reads a CSV historical file and returns a vector of (normalized time, scaled price offset) pairs.
    static std::vector<std::pair<double, int>> getOffsetEventList(const std::string &historical_data_file) {

        std::ifstream file(historical_data_file);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open the file: " + historical_data_file);
        }
        std::vector<std::pair<double, double>> raw_events;
        std::string line;
        bool first_line = true;
        double first_time = 0.0;
        double min_price = std::numeric_limits<double>::max();
        double max_price = std::numeric_limits<double>::min();

        // Assumes CSV format: Date,Time,Open,High,Low,Close,Volume
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string date, time, open, high, low, close, volume;
            std::getline(ss, date, ',');
            std::getline(ss, time, ',');
            std::getline(ss, open, ',');
            std::getline(ss, high, ',');
            std::getline(ss, low, ',');
            std::getline(ss, close, ',');
            std::getline(ss, volume, ',');
            double price = std::stod(close);
            double current_time = parseTime(time);
            if (first_line) {
                first_time = current_time;
                first_line = false;
            }
            double elapsed_time = current_time - first_time;
            raw_events.emplace_back(elapsed_time, price);
            if (price < min_price) min_price = price;
            if (price > max_price) max_price = price;
        }
        file.close();

        if (raw_events.empty()) {
            throw std::runtime_error("No data points found in historical file: " + historical_data_file);
        }

        double total_time = raw_events.back().first;
        std::cout << "[OrderInjector] Total Time in CSV: " << total_time << " seconds\n";
        double price_range = max_price - min_price;
        int scale_factor = 40;
        std::vector<std::pair<double, int>> offset_events;

        for (const auto &event : raw_events) {
            double normalized_time = event.first / total_time;
            double normalized_price = (event.second - min_price) / price_range;
            normalized_price = std::clamp(normalized_price, 0.0, 1.0);
            int scaled_price = static_cast<int>(std::round(normalized_price * scale_factor));
            offset_events.emplace_back(normalized_time, scaled_price);
        }
        return offset_events;
    }

    // Computes the offset based on real-world data.
    static int realWorldScheduleOffset(double time, double total_time, const std::vector<std::pair<double, int>> &offset_events) {
        double percent_elapsed = time / total_time;
        percent_elapsed = std::fmod(percent_elapsed, 1.0);  // Normalize to [0, 1)
        std::cout << "Percent Elapsed: " << percent_elapsed << std::endl;
        if (percent_elapsed > 1.0) {
            percent_elapsed -= std::floor(percent_elapsed); // Keep it in [0,1]
        }
        int offset = 0;
        for (const auto &event : offset_events) {
            //std::cout << "Time: " << event.first << " | Offset: " << event.second << std::endl;
            offset = event.second;  // Keep updating offset
            if (percent_elapsed <= event.first) {
                break;  // Stop at the first event greater than percent_elapsed
            }
        }
        return offset;
    }

    // Alternative offset function (sine wave + linear trend).
    static int scheduleOffset(double time) {
        double pi2 = 2 * M_PI;
        double c = M_PI * 3000;
        double wavelength = time / c;
        double gradient = 100 * time / (c / pi2);
        double amplitude = 100 * time / (c / pi2);
        double offset = gradient + amplitude * sin(wavelength * time);
        return static_cast<int>(std::round(offset));
    }

    // Compute the next delay interval based on time mode and number of traders 
    double getNextIssueDelay(int n_traders) {
        double interval = config_->interval; // interval from config (in seconds)
        if (config_->time_mode == "periodic") 
        {
            return interval; 
        } 
        else if (config_->time_mode == "drip-fixed") 
        {
            return (n_traders > 1) ? interval / (n_traders - 1) : interval;
        } 
        else if (config_->time_mode == "drip-jitter") 
        {
            double base = (n_traders > 1) ? interval / (n_traders - 1) : interval;
            std::uniform_real_distribution<double> jitterDist(0.0, base);
            return base + jitterDist(random_generator_);
        } 
        else if (config_->time_mode == "drip-poisson") 
        {
            double lambda = (n_traders > 0) ? n_traders / interval : 1.0;
            std::exponential_distribution<double> expDist(lambda);
            return expDist(random_generator_);
        } 
        else 
        {
            // Default
            return interval;
        }
    }

    // Continuously injects orders based on the simulation configuration.
    void injectOrders() {

        //std::string csv_path = "../IBM-310817.csv"; // Path to historical data file
        //auto offset_events = getOffsetEventList(csv_path); 
        OrderInjectorConfigPtr injectorConfig = std::static_pointer_cast<OrderInjectorConfig>(config_); // Get injector configuration

        std::vector<std::pair<double, int>> offset_events;
        if (config_->use_input_file) {
            try {
                offset_events = getOffsetEventList(injectorConfig->input_file);
                std::cout << "Using input file for order schedule: " << std::endl;
            } catch (const std::exception& ex) {
                std::cerr << "[OrderInjector] Failed to load input file: " << ex.what() << "\n";
            }
        }


        std::uniform_int_distribution<> supplyMinDist(injectorConfig->supply_min_low, injectorConfig->supply_min_high);
        std::uniform_int_distribution<> supplyMaxDist(injectorConfig->supply_max_low, injectorConfig->supply_max_high);
        std::uniform_int_distribution<> demandMinDist(injectorConfig->demand_min_low, injectorConfig->demand_min_high);
        std::uniform_int_distribution<> demandMaxDist(injectorConfig->demand_max_low, injectorConfig->demand_max_high);

        int sMin = supplyMinDist(random_generator_);
        int sMax = supplyMaxDist(random_generator_);
        int dMin = demandMinDist(random_generator_);
        int dMax = demandMaxDist(random_generator_);
        std::string step_mode = injectorConfig->step_mode;

        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!is_injecting_) {
                    std::cout << "[OrderInjector] Stopping injection due to TRADING_SESSION_END event.\n";
                    break;
                }
            }

            int offset_value = 0; 
            double elapsed = getElapsedTime();
            
            if (injectorConfig->use_input_file && !offset_events.empty()) 
            {
                // Use real-world schedule offset (the total time is normalised to 1.0 here; adjust if needed)
                double total_time = offset_events.back().first;
                if (total_time <= 0.0) {
                    std::cerr << "[OrderInjector] Warning: total_time is zero or negative. Using fallback offset.\n";
                    total_time = 1.0;  // Assign a default value to avoid division by zero
                }
                offset_value = realWorldScheduleOffset(elapsed, total_time, offset_events);
                std::cout << "[OrderInjector] Real-world offset: " << offset_value << std::endl;
            } 
            else if (config_->use_offset) {
                offset_value = scheduleOffset(elapsed);
            }

            std::uniform_int_distribution<> ordersCountDist(5, 10);
            int numOrders = ordersCountDist(random_generator_);

            for (int i = 0; i < numOrders; ++i) {
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    if (!is_injecting_) {
                        std::cout << "[OrderInjector] Terminating mid-injection.\n";
                        return;
                    }
                }
                injectSingleOrder(sMin, sMax, dMin, dMax, offset_value, step_mode);
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
            }
            double delay = getNextIssueDelay(trader_addresses_.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
        }
        std::cout << "[OrderInjector] Finished active injection.\n";
    }

    // Injecting single order into stock exchange. 
    void injectSingleOrder(int sMin, int sMax, int dMin, int dMax, int offset_value, const std::string &step_mode) {

        Order::Side side = (std::rand() % 2 == 0) ? Order::Side::BID : Order::Side::ASK; // Randomly select side
        int base_price = 0;
        if (side == Order::Side::ASK) 
        {
            std::uniform_int_distribution<> dist(sMin, sMax);
            base_price = dist(random_generator_);
        } else 
        {
            std::uniform_int_distribution<> dist(dMin, dMax);
            base_price = dist(random_generator_);
        }

        int final_price = std::clamp(base_price + offset_value, 1, 9999); // Clamp final price between 1 and 9999

        if (step_mode == "jittered") {
            std::uniform_int_distribution<int> jitterDist(-2, 2);
            final_price += jitterDist(random_generator_);
        } 
        else if (step_mode == "random") 
        {
            // Re-pick price randomly from the appropriate range.
            if (side == Order::Side::ASK) 
            {
                std::uniform_int_distribution<> dist(sMin, sMax);
                final_price = dist(random_generator_);
            } else 
            {
                std::uniform_int_distribution<> dist(dMin, dMax);
                final_price = dist(random_generator_);
            }
        }

        // Create customer order message 
        CustomerOrderMessagePtr customer_msg = std::make_shared<CustomerOrderMessage>();// Create limit order message
        customer_msg->client_order_id = next_client_order_id_++; 
        customer_msg->ticker = ticker_;
        customer_msg->side = side;
        std::uniform_int_distribution<int> dist(10, 50);
        customer_msg->quantity = dist(random_generator_);
        customer_msg->price = final_price;
        customer_msg->priv_value = -1.0; // if you need or want some “private value”

        // Sending orders to traders
        if (trader_addresses_.empty()) { 
            std::cerr << "[OrderInjector] No trader addresses configured. Cannot inject.\n";
            return; 
        }
        std::uniform_int_distribution<size_t> pickTraderDist(0, trader_addresses_.size()-1);
        std::string target_trader = trader_addresses_[pickTraderDist(random_generator_)];

        try {
            sendBroadcast(target_trader, std::static_pointer_cast<Message>(customer_msg));
            std::cout << "[OrderInjector] Sent customer order to trader (" << target_trader << "): "<< (side == Order::Side::BID ? "BID" : "ASK") << " @ " << final_price << std::endl;
        }
        catch (const std::runtime_error &e) {
            std::cerr << "[OrderInjector] Failed to send customer order to trader (" << target_trader
                      << "): " << e.what() << std::endl;
        } 

        //std::cout << "[OrderInjector] Sent customer order to trader (" << target_trader << "): " << (side == Order::Side::BID ? "BID" : "ASK") << " @ " << final_price << std::endl;
    }
};

#endif
