#ifndef ORCHESTRATOR_AGENT_HPP
#define ORCHESTRATOR_AGENT_HPP

#include "agent.hpp"
#include "agenttype.hpp"
#include "../config/simulationconfig.hpp"
#include "../config/agentconfig.hpp"
#include "../config/exchangeconfig.hpp"
#include "../config/marketwatcherconfig.hpp"
#include "../config/orderschedule.hpp"
#include "../message/config_message.hpp"
#include "../message/limit_order_message.hpp"
#include "../message/config_message.hpp"
#include "../order/order.hpp"

#include <sys/stat.h> 
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <random>
#include <thread>


class OrchestratorAgent : public Agent 
{
public: 

    OrchestratorAgent(NetworkEntity *network_entity, AgentConfigPtr config)
    : Agent(network_entity, config)
    {
    }

    /** Configures the simulation given a simulation configuration. */
    void configureSimulation(SimulationConfigPtr simulation)
    {
        configuration_thread_ = new std::thread([&](){
            std::cout << "Simulation repetitions: " << simulation->repetitions() 
            << " time: " << simulation->time() << " seconds." << std::endl;

            for (int i = 0; i < simulation->repetitions(); i++)
            {
                // Initialise exchanges
                for (auto exchange_config : simulation->exchanges())
                {
                    configureNode(exchange_config);
                }

                // Allow exchanges to initialise first
                std::this_thread::sleep_for(std::chrono::seconds(10));

                // Initialise traders
                for (auto trader_config : simulation->traders())
                {   
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    launchTraderProcess(trader_config->addr, to_string(trader_config->type));
                    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Wait for traders to launch
                    configureNode(trader_config);
                }

                // Initialise watchers
                for (auto watcher_config : simulation->watchers())
                {
                    std::cout << "Initialising watcher: "
                                << watcher_config->addr 
                                << " for exchange " 
                                << std::dynamic_pointer_cast<MarketWatcherConfig>(watcher_config)->exchange_name
                                << " with ticker "
                                << std::dynamic_pointer_cast<MarketWatcherConfig>(watcher_config)->ticker 
                                << std::endl;
                    configureNode(watcher_config);
                }

                generateCustomerOrders(simulation); // Generate customer orders for S/D variability.

                // Wait for this trial to finish before starting the next one
                std::cout << "Simulation " << i << " configured." << std::endl;
                std::cout << "Waiting " << simulation->time() << " seconds for simulation trial to end..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(simulation->time()));
            }
            std::cout << "Finished all " << simulation->repetitions() << " simulation trials." << std::endl;
        });
    }

    /** Sends a config message to the simulation node at the given address. */
    void configureNode(AgentConfigPtr config)
    {   
        //std::cout << "sending config message to: " << config->addr << std::endl; // DEBUG
        std::cout << "Initialising agent: " << to_string(config->type) << " with addr: " << config->addr << "\n"; // DEBUG
        this->connect(std::string(config->addr), std::to_string(config->agent_id), [=, this](){

            ConfigMessagePtr msg = std::make_shared<ConfigMessage>();
            msg->config = config;

            std::string agent_id = std::to_string(config->agent_id);
            //std::cout << "sending config to agent " << agent_id << " at " << config->addr << std::endl; DEBUG
            this->sendMessageTo(agent_id, std::static_pointer_cast<Message>(msg));
        });
    }

    /** Launches a trader process at the given address to store logs as substitute to terminal prints when using markets.csv. */
    void launchTraderProcess(const std::string& addr, const std::string& trader_type) {
        
        // Clear logs directory 
        static bool logsCleared = false;
        if (!logsCleared) { // Clear logs directory only once
            int clear_logs_ret = system("rm -rf logs/*"); // Clear logs directory
            if (clear_logs_ret != 0) { // Check if logs directory was cleared
                std::cerr << "Error: Failed to clear logs directory." << std::endl; 
            } else { // If logs directory was cleared
                std::cout << "Cleared logs directory completely." << std::endl; 
            }
            logsCleared = true;
        }
        
        std::string port = addr.substr(addr.find(":") + 1); // Extract port from address
    
        // Ensure logs and logs/traders directory exist
        struct stat info; // Check if logs directory exists
        if (stat("logs", &info) != 0 || !S_ISDIR(info.st_mode)) {  // If logs directory does not exist
            if (mkdir("logs", 0777) != 0) { // Create logs directory. 0777 = read, write, execute permissions for all users
                std::cerr << "Error: Failed to create logs directory.\n"; 
            } else { // If logs directory was created
                std::cout << "Created logs directory..." << std::endl;
            }
        }
        
        if (stat("logs/traders", &info) != 0) {  // If logs/traders directory does not exist
            if (mkdir("logs/traders", 0777) != 0) { // Create logs/traders directory
                std::cerr << "Error: Failed to create logs/traders directory.\n";
            } else {
                std::cout << "Created logs/traders directory..." << "\n";
            }
        }

        std::string log_path = "logs/traders/trader_" + port + ".log"; // Set log path

        //if (remove(log_path.c_str()) == 0) { // Clear previous log
            //std::cout << "Cleared previous log: " << log_path << std::endl; // DEBUG
        //}
        //else {  If failed to clear previous log
            //std::cerr << "Failed to clear previous log: " << log_path << std::endl; // DEBUG
        //}

        std::string command = "nohup ./simulation node --port " + port + " > " + log_path + " 2>&1 &"; // Set command to launch trader process with log

        std::cout << "Launching " << trader_type << " trader at: " << addr << " (log: " << log_path << ")" << "\n"; // DEBUG
        
        int ret = system(command.c_str()); // Launch trader process
        //std::cout << "Launch return code for " << addr << ": " << ret << std::endl; // DEBUG
        if (ret != 0) {
            std::cerr << "Failed to launch " << trader_type << " trader at: " << addr << "\n"; 
        }
    };

    /** Generate offset-based orders every second until simulation->time() expires for S/D variability. */
    void generateCustomerOrders(SimulationConfigPtr simulation) 
    { 

        // Pull the schedule info (with offsets) out of the simulation config 

        // Read CSV and store in schedule 
        std::string csv_path = "../IBM-310817.csv";
        // Check if there is a schedule, otherwise can't do offset-based orders 
        OrderSchedulePtr schedule = simulation->orderSchedule();
        if(!schedule) { 
            std::cerr << "No order schedule found in simulation config -- cannot generate offset-based orders.\n" << std::endl;
            return;
        } else {
            schedule->offset_events = getOffsetEventList(csv_path);
        }

        auto &events = schedule->offset_events; 
        double total_time = simulation->time();

        // Assume a single exchnage 
        if (simulation->exchanges().empty()) { 
            std::cerr << "No exchanges found in simulation config -- cannot generate offset-based orders.\n" << std::endl;
            return; 
        }

        // Get the first exchange details
        auto exchange_config = simulation->exchanges().front(); 
        std::string exchange_addr = exchange_config->addr;
        int exchange_id = exchange_config->agent_id;
        std::string ticker = exchange_config->tickers.front(); 

        // Decide supply & demand ranges 
        std::random_device rd; 
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> supplyMinDist(schedule->supply_min_low,  schedule->supply_min_high);
        std::uniform_int_distribution<> supplyMaxDist(schedule->supply_max_low,  schedule->supply_max_high);
        std::uniform_int_distribution<> demandMinDist(schedule->demand_min_low, schedule->demand_min_high);
        std::uniform_int_distribution<> demandMaxDist(schedule->demand_max_low, schedule->demand_max_high);

        // Sample supply & demand ranges
        int sMin = supplyMinDist(gen);
        int sMax = supplyMaxDist(gen);
        int dMin = demandMinDist(gen);
        int dMax = demandMaxDist(gen);

        // For each second in real time 
        double total_seconds = simulation->time();

        for (int current_second = 0; current_second < (int)total_time; ++current_second) 
        { 
            int offset_value = 0; 
            if(!events.empty()) { 
                offset_value = realWorldScheduleOffset(current_second, total_time, events); 
            }
            else { 
                offset_value = scheduleOffset(current_second);
            }

            // Randomly decide side or do a 50/50 e.g. if even => BID, if odd => ASK. Could be random - CHECK TBSE.PY
            // TBSE.PY - SENDS 3 BUY ADN 3 SELL ORDERS EVERY SECOND. MINE JUST GENERATES BUYS/SELLS BASED ON ODD/EVEN TIME SECOND. PERHAPS LATER FIX.
            Order::Side side = (current_second % 2 == 0) ? Order::Side::BID : Order::Side::ASK; 

            // Pick a base limit from the supply and demand
            // SELL => pick from [sMin, sMax], BUY => pick from [dMin, dMax]
            int base_price = 0;
            if (side == Order::Side::ASK) { 
                std::uniform_int_distribution<> dist(sMin, sMax);
                int base_price = dist(gen);
            }
            else { 
                std::uniform_int_distribution<> dist(dMin, dMax);
                int base_price = dist(gen);
            }

            int final_price = base_price + offset_value;
            if (final_price < 1) final_price = 1; 
            if (final_price > 9999) final_price = 9999;

            // Create the limit order
            LimitOrderMessagePtr order_msg = std::make_shared<LimitOrderMessage>();
            order_msg->sender_id = 999; // Orchestrator 
            order_msg->ticker = ticker; 
            order_msg->side = side; // SIDE
            order_msg->quantity = 100; 
            order_msg->price = final_price;
            order_msg->time_in_force = Order::TimeInForce::GTC;
            order_msg->agent_name = "Orchestrator";


            // Send limit order to exchange
            this->connect(exchange_addr, std::to_string(exchange_id), [=, this](){
                this->sendMessageTo(std::to_string(exchange_id), std::static_pointer_cast<Message>(order_msg));
            });


            // Variability every second
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::cout << "[Orchestrator] Finished generating offset-based orders.\n";
    }

private:

    /** Checks the type of the incoming message and makes a callback. */
    std::optional<MessagePtr> handleMessageFrom(std::string_view sender, MessagePtr message) override
    {
        std::cout << "Orchestrator received a message" << "\n";
        return std::nullopt;
    }

    /** Checks the type of the incoming broadcast and makes a callback. */
    void handleBroadcastFrom(std::string_view sender, MessagePtr message) override 
    {
        std::cout << "Orchestrator received a broadcast" << "\n";
    }

    std::thread* configuration_thread_;
};

#endif
