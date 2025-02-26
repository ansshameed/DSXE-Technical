#ifndef ORDER_INJECTOR_AGENT_HPP
#define ORDER_INJECTOR_AGENT_HPP

#include "agent.hpp"
#include "traderagent.hpp"
#include "../config/simulationconfig.hpp"
#include "../config/orderinjectorconfig.hpp"
#include "../message/limit_order_message.hpp"
#include "../order/order.hpp"
#include <random>
#include <thread>
#include <mutex>
#include <optional>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <iomanip>
#include <cmath>
#include <limits>
#include <algorithm>


class OrderInjectorAgent : public TraderAgent 
{
public:
    OrderInjectorAgent(NetworkEntity* network_entity, OrderInjectorConfigPtr config)
        : TraderAgent(network_entity, config), 
          exchange_{config->exchange_name},  // Store exchange name
          ticker_{config->ticker},           // Store ticker symbol
          injection_thread_(nullptr), 
          is_injecting_(false),
          random_generator_(std::random_device{}()), 
          config_(config)
        {
            // Connect to exchange and subscribe to market data
            connect(config->exchange_addr, config->exchange_name, [=, this]() {
                subscribeToMarket(config->exchange_name, config->ticker);
            });
    }   

    ~OrderInjectorAgent() { terminate(); }

    std::string getAgentName() const override { return "OrderInjector"; }

    // Called when the trading session starts.
    void onTradingStart() override {
        std::cout << "[OrderInjector] Trading window started.\n";
        is_injecting_ = true;
        start_time_ = std::chrono::high_resolution_clock::now();
        activelyInject();
    }


    // Called when the trading session ends.
    void onTradingEnd() override {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            is_injecting_ = false;
        }
        if (injection_thread_ != nullptr) {
            injection_thread_->join();
            delete injection_thread_;
            injection_thread_ = nullptr;
        }
        std::cout << "[OrderInjector] Trading window ended.\n";
    }

    void onMarketData(std::string_view exchange, MarketDataMessagePtr msg) override {
        std::cout << "[OrderInjector] Market data received from " << exchange << ".\n";
    }

    void onExecutionReport(std::string_view exchange, ExecutionReportMessagePtr msg) override {
        std::cout << "[OrderInjector] Execution report received from " << exchange << ".\n";
    }

    void onCancelReject(std::string_view exchange, CancelRejectMessagePtr msg) override {
        std::cout << "[OrderInjector] Order cancellation rejected from " << exchange << ".\n";
    }

    // Gracefully terminates the agent.
    void terminate() override {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            is_injecting_ = false;
        }
        if (injection_thread_ != nullptr) {
            if (injection_thread_->joinable()) {
                injection_thread_->join();
            }
            delete injection_thread_;
            injection_thread_ = nullptr;
        }
        std::cout << "[OrderInjector] Successfully terminated.\n";
    }
    

    void handleBroadcastFrom(std::string_view sender, MessagePtr message) override
    {
        if (message->type == MessageType::EVENT) {
            EventMessagePtr eventMsg = std::dynamic_pointer_cast<EventMessage>(message);
            if (!eventMsg) {
                throw std::runtime_error("Failed to cast message to EventMessage");
            }

            if (eventMsg->event_type == EventMessage::EventType::ORDER_INJECTION_START) {
                std::cout << "[OrderInjector] Received ORDER_INJECTION_START event. Beginning order injection.\n";
                onTradingStart();
            } 
            else if (eventMsg->event_type == EventMessage::EventType::TRADING_SESSION_START) {
                std::cout << "[OrderInjector] Trading session started. Restarting order injection if needed.\n";
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    if (!is_injecting_) {  // Restart injection only if it was stopped
                        is_injecting_ = true;
                        activelyInject();
                    }
                }
            }
            else if (eventMsg->event_type == EventMessage::EventType::TRADING_SESSION_END) {
                std::cout << "[OrderInjector] Trading session ended. Stopping order injection.\n";
                onTradingEnd();
                terminate(); 
            }
        } 
        else if (message->type == MessageType::MARKET_DATA) {
            MarketDataMessagePtr mdMsg = std::dynamic_pointer_cast<MarketDataMessage>(message);
            if (mdMsg) {
                std::cout << "[OrderInjector] Received market data broadcast from " << sender << ".\n";
                onMarketData(sender, mdMsg);
            }
        } 
        else {
            // Ignore or handle other message types as needed.
            return;
        }
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

    void activelyInject() {
        injection_thread_ = new std::thread([this]() {
            injectOrders();
        });
    }

    double getElapsedTime() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(now - start_time_).count();
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
        double price_range = max_price - min_price;
        int scale_factor = 80;
        std::vector<std::pair<double, int>> offset_events;

        for (const auto &event : raw_events) {
            double normalized_time = event.first / total_time;
            double normalized_price = (event.second - min_price) / price_range;
            normalized_price = std::clamp(normalized_price, 0.0, 1.0);
            int scaled_price = static_cast<int>(normalized_price * scale_factor);
            offset_events.emplace_back(normalized_time, scaled_price);
        }
        return offset_events;
    }

    // Computes the offset based on real-world data.
    static int realWorldScheduleOffset(double time, double total_time, const std::vector<std::pair<double, int>> &offset_events) {
        double percent_elapsed = time / total_time;
        int offset = 0;
        for (const auto &event : offset_events) {
            if (percent_elapsed <= event.first) {
                offset = event.second;
                break;
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

    // Continuously injects orders based on the simulation configuration.
    void injectOrders() {

        std::string csv_path = "../IBM-310817.csv"; // Path to historical data file
        auto offset_events = getOffsetEventList(csv_path); 

        OrderInjectorConfigPtr injectorConfig = std::static_pointer_cast<OrderInjectorConfig>(config_); // Get injector configuration
        std::uniform_int_distribution<> supplyMinDist(injectorConfig->supply_min_low, injectorConfig->supply_min_high);
        std::uniform_int_distribution<> supplyMaxDist(injectorConfig->supply_max_low, injectorConfig->supply_max_high);
        std::uniform_int_distribution<> demandMinDist(injectorConfig->demand_min_low, injectorConfig->demand_min_high);
        std::uniform_int_distribution<> demandMaxDist(injectorConfig->demand_max_low, injectorConfig->demand_max_high);

        int sMin = supplyMinDist(random_generator_);
        int sMax = supplyMaxDist(random_generator_);
        int dMin = demandMinDist(random_generator_);
        int dMax = demandMaxDist(random_generator_);

        while (true) { // Continuously inject orders
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!is_injecting_)
                    break;
            }

            int offset_value = 0; 
            if (!offset_events.empty()) {
                offset_value = realWorldScheduleOffset(getElapsedTime(), 1.0, offset_events);
            } else {
                offset_value = scheduleOffset(getElapsedTime());
            }

            std::uniform_int_distribution<> ordersCountDist(2, 5);
            int numOrders = ordersCountDist(random_generator_);

            for (int i = 0; i < numOrders; ++i) {
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    if (!is_injecting_) {
                        std::cout << "[OrderInjector] Terminating mid-injection.\n";
                        return;
                    }
                }
                injectSingleOrder(sMin, sMax, dMin, dMax, offset_value);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        std::cout << "[OrderInjector] Finished active injection.\n";
    }

    // Injecting single order into stock exchange. 
    void injectSingleOrder(int sMin, int sMax, int dMin, int dMax, int offset_value) {
        Order::Side side = (std::rand() % 2 == 0) ? Order::Side::BID : Order::Side::ASK; // Randomly select side
        int base_price = (side == Order::Side::ASK)  // If side is ASK, then use demand price range, else use supply price range
        ? std::uniform_int_distribution<>(sMin, sMax)(random_generator_) 
        : std::uniform_int_distribution<>(dMin, dMax)(random_generator_);

        int final_price = std::clamp(base_price + offset_value, 1, 9999); // Clamp final price between 1 and 9999

        LimitOrderMessagePtr order_msg = std::make_shared<LimitOrderMessage>(); // Create limit order message
        order_msg->ticker = ticker_;
        order_msg->side = side;
        order_msg->quantity = 100;
        order_msg->price = final_price;
        order_msg->time_in_force = Order::TimeInForce::GTC;
        order_msg->agent_name = getAgentName();

        sendMessageTo(exchange_, std::static_pointer_cast<Message>(order_msg));

        std::cout << "[OrderInjector] Sent order: " 
        << (side == Order::Side::BID ? "BID" : "ASK") 
        << " @ " << final_price << std::endl;
    }
};

#endif
