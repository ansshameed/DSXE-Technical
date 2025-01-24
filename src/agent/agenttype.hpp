#ifndef AGENT_TYPE_HPP
#define AGENT_TYPE_HPP

enum class AgentType : int
{
    STOCK_EXCHANGE,
    MARKET_WATCHER,
    TRADER_ZIC,
    TRADER_ZIP,
    TRADER_SHVR,
    TRADER_RSI, // Relative Strength Index
    ARBITRAGE_TRADER
};

inline std::string to_string(AgentType agent_type)
{
    switch (agent_type) {
        case AgentType::STOCK_EXCHANGE: return std::string{"StockExchange"};
        case AgentType::MARKET_WATCHER: return std::string{"MarketDataWatcher"};
        case AgentType::TRADER_ZIC: return std::string{"TraderZIC"};
        case AgentType::TRADER_ZIP: return std::string{"TraderZIP"};
        case AgentType::TRADER_SHVR: return std::string{"TraderSHVR"};
        case AgentType::TRADER_RSI: return std::string{"TraderRSI"};
        case AgentType::ARBITRAGE_TRADER: return std::string{"ArbitrageTrader"};
        default: return std::string{""};
    }
}

#endif