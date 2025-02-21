#ifndef SIMULATION_CONFIG_HPP
#define SIMULATION_CONFIG_HPP

#include <memory>

#include "agentconfig.hpp"
#include "exchangeconfig.hpp"
#include "traderconfig.hpp"
#include "marketwatcherconfig.hpp"

/** Configuration object for a single simulation. */
class SimulationConfig : public std::enable_shared_from_this<SimulationConfig>
{
public:

    SimulationConfig() = delete;

    SimulationConfig(int repetitions, int time, 
                     std::vector<ExchangeConfigPtr> exchange_configs, 
                     std::vector<AgentConfigPtr> trader_configs,
                     std::vector<AgentConfigPtr> watcher_configs, 
                    std::vector<AgentConfigPtr> injector_configs) 
    : repetitions_{repetitions},
      time_{time},
      exchange_configs_{exchange_configs},
      trader_configs_{trader_configs},
      watcher_configs_{watcher_configs},
      injector_configs_{injector_configs}

    {
    }

    const std::vector<ExchangeConfigPtr>& exchanges() const { return exchange_configs_; }
    const std::vector<AgentConfigPtr>& traders() const { return trader_configs_; }
    const std::vector<AgentConfigPtr>& watchers() const { return watcher_configs_; } 
    const std::vector<AgentConfigPtr>& injectors() const { return injector_configs_; }
    int repetitions() const { return repetitions_; }
    int time() const { return time_; }

private:

    std::vector<ExchangeConfigPtr> exchange_configs_;
    std::vector<AgentConfigPtr> trader_configs_;
    std::vector<AgentConfigPtr> watcher_configs_; 
    std::vector<AgentConfigPtr> injector_configs_;
    int repetitions_;
    int time_;
};

typedef std::shared_ptr<SimulationConfig> SimulationConfigPtr;

#endif
