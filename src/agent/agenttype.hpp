#ifndef AGENT_TYPE_HPP
#define AGENT_TYPE_HPP

enum class AgentType : int
{
    STOCK_EXCHANGE,
    ORDER_INJECTOR,
    MARKET_WATCHER,
    TRADER_ZIC,
    TRADER_ZIP,
    TRADER_SHVR,
    TRADER_RSI, // Relative Strength Index
    TRADER_MACD, // Moving Average Convergence Divergence
    TRADER_OBV_DELTA, // On Balance Volume Delta
    TRADER_BOLLINGER_BANDS, // Bollinger Bands
    TRADER_VWAP, // Volume Weighted Average Price
    TRADER_RSI_BB, // RSI and Bollinger Bands
    TRADER_OBV_VWAP, // OBV and VWAP
    ARBITRAGE_TRADER, 
    TRADER_DEEP_LSTM, // DeepTrader LSTM
    TRADER_DEEP_XGB // DeepTrader XGB
};

inline std::string to_string(AgentType agent_type)
{
    switch (agent_type) {
        case AgentType::STOCK_EXCHANGE: return std::string{"StockExchange"};
        case AgentType::ORDER_INJECTOR: return std::string{"OrderInjector"};
        case AgentType::MARKET_WATCHER: return std::string{"MarketDataWatcher"};
        case AgentType::TRADER_ZIC: return std::string{"TraderZIC"};
        case AgentType::TRADER_ZIP: return std::string{"TraderZIP"};
        case AgentType::TRADER_SHVR: return std::string{"TraderSHVR"};
        case AgentType::TRADER_RSI: return std::string{"TraderRSI"};
        case AgentType::TRADER_MACD: return std::string{"TraderMACD"};
        case AgentType::TRADER_OBV_DELTA: return std::string{"TraderOBVDelta"};
        case AgentType::TRADER_BOLLINGER_BANDS: return std::string{"TraderBollingerBands"};
        case AgentType::TRADER_VWAP: return std::string{"TraderVWAP"};
        case AgentType::TRADER_RSI_BB: return std::string{"TraderBBRSI"};
        case AgentType::TRADER_OBV_VWAP: return std::string{"TraderOBVVWAP"};
        case AgentType::ARBITRAGE_TRADER: return std::string{"ArbitrageTrader"};
        case AgentType::TRADER_DEEP_LSTM: return std::string{"DeepTraderLSTM"};
        case AgentType::TRADER_DEEP_XGB: return std::string{"DeepTraderXGB"}; 
        default: return std::string{""};
    }
}

#endif