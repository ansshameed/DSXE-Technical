#ifndef AGENT_FACTORY_HPP
#define AGENT_FACTORY_HPP

#include "agent.hpp"
#include "agenttype.hpp"
#include "../config/agentconfig.hpp"


#include "stockexchange.hpp"
#include "marketdatawatcher.hpp"
#include "traderzic.hpp"
#include "traderzip.hpp"
#include "tradershvr.hpp"
#include "traderrsi.hpp"
#include "tradermacd.hpp"
#include "traderobvdelta.hpp"
#include "traderbb.hpp"
#include "tradervwap.hpp"
#include "traderrsibb.hpp"
#include "traderobvvwap.hpp"
#include "arbitragetrader.hpp"

class AgentFactory
{
public:

    AgentFactory() = delete;

    /** Creates a new instance of an agent given a configuration and returns a pointer to it. */
    static std::shared_ptr<Agent> createAgent(NetworkEntity *network_entity, AgentConfigPtr config)
    {
        switch (config->type)
        {
            case AgentType::STOCK_EXCHANGE: 
            {
                auto exchange_config = std::static_pointer_cast<ExchangeConfig>(config);
                std::shared_ptr<Agent> agent (new StockExchange{network_entity, exchange_config});
                return agent;
            }
            case AgentType::MARKET_WATCHER:
            {
                std::shared_ptr<Agent> agent (new MarketDataWatcher{network_entity, std::static_pointer_cast<MarketWatcherConfig>(config)});
                return agent;
            }
            case AgentType::TRADER_ZIC:
            {
                std::shared_ptr<Agent> agent (new TraderZIC{network_entity, std::static_pointer_cast<TraderConfig>(config)});
                return agent;
            }
            case AgentType::TRADER_ZIP:
            {
                std::shared_ptr<Agent> agent (new TraderZIP{network_entity, std::static_pointer_cast<ZIPConfig>(config)});
                return agent;
            }
            case AgentType::TRADER_SHVR:
            {
                std::shared_ptr<Agent> agent (new TraderShaver{network_entity, std::static_pointer_cast<TraderConfig>(config)});
                return agent;
            }
            case AgentType::TRADER_RSI:
            {
                int lookback = 20; // Default lookback period for RSI
                bool use_stoch_rsi = false; // Example values
                int stoch_lookback = 16; // Example values (slightly shorter than standard lookback for sensitive price changes i.e. faster signals) 
                int n_to_smooth = 2; // Example values (1 = no smoothing, higher = smoother signals i.e. reducing short-term fluctuations to identify trends easier with minimising noise from rapid price changes)

                std::shared_ptr<Agent> agent (new TraderRSI{network_entity, std::static_pointer_cast<TraderConfig>(config), lookback, use_stoch_rsi, stoch_lookback, n_to_smooth});
                return agent;
            }
            case AgentType::TRADER_MACD:
            {
                int short_period = 12; // Fast EMA period 
                int long_period = 26; // Slow EMA period 
                int signal_period = 9; // Signal line period (EMA of MACD line)
                double threshold = 0.5; // Minimum difference threshold between MACD and signal line (histogram) to trigger a trade 
                int n_to_smooth = 1; // No. of additional smoothing steps applied to MACD line (1 = minimal or no extra smoothign)
                size_t lookback_period = 14; // Lookback period for ATR calculation (normalisation factor)
                std::shared_ptr<Agent> agent (new TraderMACD{network_entity, std::static_pointer_cast<TraderConfig>(config), short_period, long_period, signal_period, threshold, n_to_smooth, lookback_period});
                return agent;
            }
            case AgentType::TRADER_OBV_DELTA: 
            { 
                int lookback_period = 14; // Default lookback period for OBV Delta
                int delta_length = 4; // Default delta length for OBV Delta
                double threshold = 10; // Default threshold for OBV Delta (5-10% of average total volume) 
                std::shared_ptr<Agent> agent (new TraderOBVDelta{network_entity, std::static_pointer_cast<TraderConfig>(config), lookback_period, delta_length, threshold});
                return agent;
            }
            case AgentType::TRADER_BOLLINGER_BANDS: 
            {   
                int lookback_period = 14; 
                double std_dev_multiplier = 2.0; // Default standard deviation multiplier for Bollinger Bands
                std::shared_ptr<Agent> agent (new TraderBollingerBands{network_entity, std::static_pointer_cast<TraderConfig>(config), lookback_period, std_dev_multiplier});
                return agent;
            }
            case AgentType::TRADER_VWAP: 
            { 
                int lookback_period = 14; // Default lookback period for VWAP
                std::shared_ptr<Agent> agent (new TraderVWAP{network_entity, std::static_pointer_cast<TraderConfig>(config), lookback_period});
                return agent;
            }
            case AgentType::TRADER_RSI_BB: 
            { 
                int lookback_bb = 14; // Default lookback period for Bollinger Bands
                int lookback_rsi = 20; // Default lookback period for RSI
                double std_dev_multiplier = 2.0; // Default standard deviation multiplier for Bollinger Bands
                std::shared_ptr<Agent> agent (new TraderBBRSI{network_entity, std::static_pointer_cast<TraderConfig>(config), lookback_bb, lookback_rsi, std_dev_multiplier});
                return agent;
            }
            case AgentType::TRADER_OBV_VWAP: 
            { 
                int lookback_vwap = 15; // Default lookback period for VWAP
                int lookback_obv = 12; // Default lookback period for OBV
                int delta_length = 5; // Default delta length for OBV Delta
                double threshold = 5; // Default threshold for OBV Delta (5-10% of average total volume) 
                std::shared_ptr<Agent> agent (new TraderVWAPOBVDelta{network_entity, std::static_pointer_cast<TraderConfig>(config), lookback_vwap, lookback_obv, delta_length, threshold});
                return agent;
            }
            case AgentType::ARBITRAGE_TRADER:
            {
                std::shared_ptr<Agent> agent (new ArbitrageTrader{network_entity, std::static_pointer_cast<ArbitrageurConfig>(config)});
                return agent;
            }
            default:
            {
                throw std::runtime_error("Failed to create agent. Unknown agent received");
            }
        }
    }

    /** Returns the agent type corresponding to the given XML tag. */
    static AgentType getAgentTypeForTag(std::string& xml_tag) {
        if (xml_tag_map.find(xml_tag) == xml_tag_map.end())
        {
            throw std::runtime_error("XML Configuration Error. Cannot identify the agent for tag: " + xml_tag);
        }

        return xml_tag_map.at(xml_tag);
    }

private:

    /** Map of XML tags and the corresponding agent types. */
    static inline const std::unordered_map<std::string, AgentType> xml_tag_map {
        {std::string{"exchange"}, AgentType::STOCK_EXCHANGE},
        {std::string{"watcher"}, AgentType::MARKET_WATCHER},
        {std::string{"zic"}, AgentType::TRADER_ZIC},
        {std::string{"zip"}, AgentType::TRADER_ZIP},
        {std::string{"shvr"}, AgentType::TRADER_SHVR},
        {std::string{"rsi"}, AgentType::TRADER_RSI},
        {std::string{"macd"}, AgentType::TRADER_MACD},
        {std::string{"obvd"}, AgentType::TRADER_OBV_DELTA},
        {std::string{"bb"}, AgentType::TRADER_BOLLINGER_BANDS},
        {std::string{"vwap"}, AgentType::TRADER_VWAP},
        {std::string{"rsibb"}, AgentType::TRADER_RSI_BB},
        {std::string{"obvvwap"}, AgentType::TRADER_OBV_VWAP},
        {std::string{"arbitrageur"}, AgentType::ARBITRAGE_TRADER}
    };

};

#endif