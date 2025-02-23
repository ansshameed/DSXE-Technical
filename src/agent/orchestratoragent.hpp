#ifndef ORCHESTRATOR_AGENT_HPP
#define ORCHESTRATOR_AGENT_HPP

#include "agent.hpp"
#include "agenttype.hpp"
#include "../config/simulationconfig.hpp"
#include "../config/agentconfig.hpp"
#include "../config/exchangeconfig.hpp"
#include "../config/marketwatcherconfig.hpp"
#include "../config/orderinjectorconfig.hpp"
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
                    std::this_thread::sleep_for(std::chrono::seconds(2)); // Wait for traders to launch
                    configureNode(trader_config);
                }

                // Allow traders to initialise first
                std::this_thread::sleep_for(std::chrono::seconds(5));

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

                for (auto injector_config : simulation->injectors())
                {
                    std::cout << "Initialising injector: "
                                << injector_config->addr 
                                << " for exchange " 
                                << std::dynamic_pointer_cast<OrderInjectorConfig>(injector_config)->exchange_name
                                << std::endl;
                    configureNode(injector_config);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    launchOrderInjectorProcess(injector_config->addr, to_string(injector_config->type));
                }

                // Allow watcher to initialise first
                std::this_thread::sleep_for(std::chrono::seconds(2));

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
        std::string command = "nohup ./simulation node --port " + port + " > " + log_path + " 2>&1 &"; // Set command to launch trader process with log
        std::cout << "Launching " << trader_type << " trader at: " << addr << " (log: " << log_path << ")" << "\n"; // DEBUG
        
        int ret = system(command.c_str()); // Launch trader process
        //std::cout << "Launch return code for " << addr << ": " << ret << std::endl; // DEBUG
        if (ret != 0) {
            std::cerr << "Failed to launch " << trader_type << " trader at: " << addr << "\n"; 
        }
    };

    void launchOrderInjectorProcess(const std::string& addr, const std::string& injector_type) {
        std::cout << "Configuring " << injector_type << " injector at: " << addr << "\n";
    
        this->connect(addr, injector_type, [=, this](){
            ConfigMessagePtr msg = std::make_shared<ConfigMessage>();
            this->sendMessageTo(injector_type, std::static_pointer_cast<Message>(msg));
        });
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
