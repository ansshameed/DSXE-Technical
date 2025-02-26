#include "configreader.hpp"
#include "../agent/agentfactory.hpp"

SimulationConfigPtr ConfigReader::readConfig(std::string& filepath)
{
    pugi::xml_document doc;
    pugi::xml_parse_result res = doc.load_file(filepath.c_str());
    if (res.status != pugi::xml_parse_status::status_ok)
    {
        throw std::runtime_error("Failed to load configuration file: " + filepath);
    }

    pugi::xml_node simulation = doc.child("simulation");

    // Parse through the general configuration
    pugi::xml_node parameters = simulation.child("parameters");
    int time = parameters.child("time").text().as_int(120); // Default to 120 seconds
    int repetitions = parameters.child("repetitions").text().as_int(1); // Default to 1 repetition
    
    // Parse through the available instances
    std::vector<std::string> exchange_addrs;
    std::vector<std::string> trader_addrs;
    std::vector<std::string> watcher_addrs;
    std::vector<std::string> injector_addrs;
    
    pugi::xml_node instances = simulation.child("instances");
    for (auto instance : instances.children())
    {
        std::string ip {instance.attribute("ip").value()};
        std::string port {instance.attribute("port").value()};
        std::string addr { ip + std::string{":"} + port };

        if (std::string{instance.attribute("agent-type").value()} == std::string{"exchange"})
        {
            exchange_addrs.push_back(addr);
        }
        else if (std::string{instance.attribute("agent-type").value()} == "trader")
        {
            trader_addrs.push_back(addr);
        }
        else if (std::string{instance.attribute("agent-type").value()} == "watcher")
        {
            watcher_addrs.push_back(addr);
        }
        else if (std::string{instance.attribute("agent-type").value()} == "orderinjector")
        {
            injector_addrs.push_back(addr);
        }
    }

    // Parse through the configured agents
    int agent_id = 0;
    pugi::xml_node agents = simulation.child("agents");

    // Exchanges
    std::unordered_map<std::string, std::string> exchange_addrs_map; 
    std::vector<ExchangeConfigPtr> exchange_configs;
    int instance_id = 0;
    pugi::xml_node exchanges = agents.child("exchanges");
    for (auto exchange : exchanges.children())
    {
        ExchangeConfigPtr exchange_config = configureExchange(agent_id, exchange, exchange_addrs.at(instance_id));
        exchange_addrs_map.insert({exchange_config->name, exchange_addrs.at(instance_id)});
        exchange_configs.push_back(exchange_config);
        ++instance_id;
        ++agent_id; 
    }

    // Get the default exchange name and ticker from exchange name for traders to use. - CHANGE LOGIC FOR MULTIPLE EXCHANGES. 
    std::string default_exchange_name; 
    std::string default_ticker;
    if (!exchange_configs.empty()) {
        default_exchange_name = exchange_configs.at(0)->name;
        if (!exchange_configs.at(0)->tickers.empty()) {
            default_ticker = exchange_configs.at(0)->tickers.at(0);
        }
    }


    // Traders - XML VERSION for manual allocation in simulation config file. 
    //std::vector<AgentConfigPtr> trader_configs;
    //instance_id = 0;
    //pugi::xml_node traders = agents.child("traders");
    //for (auto trader : traders.children())
    //{
        //AgentConfigPtr agent_config = configureAgent(agent_id, trader, trader_addrs.at(instance_id), exchange_addrs_map);
        //trader_configs.push_back(agent_config);

        // Store agent name mapping DBEBUG FOR AGENT NAME
        //std::string trader_name = trader.name();
        //std::cout << "Reading trader name: " << trader_name << " for agent ID: " << agent_id << std::endl;
        //agent_names[agent_id] = trader_name; 

        //++instance_id;
        //++agent_id;
    //} 

    // Traders - CSV VERSION for dynamic allocation from markets.csv
    std::string csv_filepath = "../markets.csv"; // Default path for markets.csv
    // Use a command-line argument if provided
    if (filepath.find(".csv") != std::string::npos) { // Check if the filepath is a CSV file
        csv_filepath = filepath; // Set the CSV filepath
    }
    std::cout << "Loading trader configurations from " << csv_filepath << "..." << std::endl; // DEBUG
    SimulationConfigPtr csv_config = readConfigFromCSV(csv_filepath, exchange_addrs_map, agent_id, default_exchange_name, default_ticker); // Read trader configurations from CSV
    std::vector<AgentConfigPtr> trader_configs = csv_config->traders(); // Get trader configurations from CSV

    // Watchers
    std::vector<AgentConfigPtr> watcher_configs;
    int watcher_instance_id = 0;
    pugi::xml_node watchers = simulation.child("agents").child("watchers");
    for (auto watcher : watchers.children())
    {
        AgentConfigPtr watcher_config = configureMarketWatcher(
            agent_id,
            watcher,
            watcher_addrs.at(watcher_instance_id),
            exchange_addrs_map
        );
        watcher_configs.push_back(watcher_config);
        ++watcher_instance_id;
        ++agent_id;
    }

    // Injectors 
    std::vector<AgentConfigPtr> injector_configs;
    int injector_instance_id = 0;
    pugi::xml_node injectors = simulation.child("agents").child("orderinjectors");
    for (auto injector : injectors.children())
    {
        AgentConfigPtr injector_config = configureOrderInjector(
            agent_id,
            injector,
            injector_addrs.at(injector_instance_id),
            exchange_addrs_map
        );
        injector_configs.push_back(injector_config);
        ++injector_instance_id;
        ++agent_id;
    }

    SimulationConfigPtr simulation_config = std::make_shared<SimulationConfig>(repetitions, time, exchange_configs, trader_configs, watcher_configs, injector_configs);
    return simulation_config;
}

AgentConfigPtr ConfigReader::configureAgent(int id, pugi::xml_node& xml_node, std::string& addr, std::unordered_map<std::string, std::string>& exchange_addrs)
{
    std::string type_tag { xml_node.name() };
    AgentType type = AgentFactory::getAgentTypeForTag(type_tag);

    switch (type)
    {
        case AgentType::TRADER_ZIC:
        {
            return configureTrader(id, xml_node, addr, exchange_addrs, type);
        }
        case AgentType::TRADER_ZIP:
        {
            return configureTraderZIP(id, xml_node, addr, exchange_addrs);
        }
        case AgentType::TRADER_SHVR:
        {
            return configureTrader(id, xml_node, addr, exchange_addrs, type);
        }
        case AgentType::TRADER_RSI:
        {
            return configureTrader(id, xml_node, addr, exchange_addrs, type);
        }
        case AgentType::TRADER_MACD:
        {
            return configureTrader(id, xml_node, addr, exchange_addrs, type);
        }
        case AgentType::TRADER_OBV_DELTA:
        {
            return configureTrader(id, xml_node, addr, exchange_addrs, type);
        }
        case AgentType::TRADER_BOLLINGER_BANDS: 
        { 
            return configureTrader(id, xml_node, addr, exchange_addrs, type);
        }
        case AgentType::TRADER_VWAP:
        {
            return configureTrader(id, xml_node, addr, exchange_addrs, type);
        }
        case AgentType::TRADER_RSI_BB:
        {
            return configureTrader(id, xml_node, addr, exchange_addrs, type);
        }
        case AgentType::TRADER_OBV_VWAP:
        {
            return configureTrader(id, xml_node, addr, exchange_addrs, type);
        }
        case AgentType::ARBITRAGE_TRADER:
        {
            return configureArbitrageur(id, xml_node, addr, exchange_addrs);
        }
        case AgentType::MARKET_WATCHER:
        {
            return configureMarketWatcher(id, xml_node, addr, exchange_addrs);
        }
        case AgentType::ORDER_INJECTOR:
        {
            return configureOrderInjector(id, xml_node, addr, exchange_addrs);
        }
        default:
        {
            throw std::runtime_error("Unknown XML tag in configuration file");
        }
    }
}

ExchangeConfigPtr ConfigReader::configureExchange(int id, pugi::xml_node& xml_node, std::string& addr)
{
    ExchangeConfigPtr exchange_config = std::make_shared<ExchangeConfig>();

    exchange_config->agent_id = id;
    
    std::string type_tag { xml_node.name() };
    exchange_config->type = AgentFactory::getAgentTypeForTag(type_tag);

    exchange_config->addr = addr;
    exchange_config->name = std::string{xml_node.attribute("name").value()};
    exchange_config->tickers = std::vector{std::string{xml_node.attribute("ticker").value()}};
    exchange_config->connect_time = std::atoi(xml_node.attribute("connect-time").value());
    exchange_config->trading_time = std::atoi(xml_node.attribute("trading-time").value());

    return exchange_config;
}

AgentConfigPtr ConfigReader::configureTrader(int id, pugi::xml_node& xml_node, 
    std::string& addr, std::unordered_map<std::string, std::string>& exchange_addrs, 
    AgentType trader_type)
{
    TraderConfigPtr trader_config = std::make_shared<TraderConfig>();
    trader_config->agent_id = id;

    //std::string type_tag { xml_node.name() }; // PROFITABILITY XML NAME DEBUG
    //exchange_config->type = AgentFactory::getAgentTypeForTag(type_tag);

    // FIX: Ensure trader name is set from XML for profitability snapshot recognition identity. 
    if (xml_node.attribute("name")) {
        trader_config->name = xml_node.attribute("name").as_string();
    } else {
        trader_config->name = "Trader_" + std::to_string(id); // Default Name
        std::cerr << " Warning: Trader name missing in XML! Assigning default name: " << trader_config->name << std::endl;
    }

    trader_config->type = trader_type;
    trader_config->addr = addr;
    trader_config->exchange_name = xml_node.attribute("exchange").as_string();
    trader_config->exchange_addr = exchange_addrs.at(trader_config->exchange_name);
    trader_config->ticker = xml_node.attribute("ticker").as_string();

    trader_config->limit = xml_node.attribute("limit").as_int(50);
    trader_config->trade_interval = xml_node.attribute("trade-interval").as_int(1);
    trader_config->delay = xml_node.attribute("delay").as_int(0);

    std::string cancelling = xml_node.attribute("cancel").as_string();
    trader_config->cancelling = (cancelling == "true");

    std::string side = xml_node.attribute("side").as_string();
    trader_config->side = (side == "buy") ? Order::Side::BID : Order::Side::ASK;

    return std::static_pointer_cast<AgentConfig>(trader_config);
}

AgentConfigPtr ConfigReader::configureArbitrageur(int id, pugi::xml_node& xml_node, std::string& addr, std::unordered_map<std::string, std::string>& exchange_addrs)
{
    ArbitrageurConfigPtr config = std::make_shared<ArbitrageurConfig>();
    config->agent_id = id;
    config->addr = addr;

    std::string type_tag { xml_node.name() };
    config->type = AgentFactory::getAgentTypeForTag(type_tag);

    config->exchange0_name = std::string{xml_node.attribute("exchange0").value()};
    config->exchange0_addr = exchange_addrs.at(config->exchange0_name);

    config->exchange1_name = std::string{xml_node.attribute("exchange1").value()};
    config->exchange1_addr = exchange_addrs.at(config->exchange1_name);
    
    config->ticker = std::string{xml_node.attribute("ticker").value()};
    config->alpha = std::stod(xml_node.attribute("alpha").value());
    config->delay = std::atoi(xml_node.attribute("delay").value());
    config->trade_interval = std::stoul(xml_node.attribute("trade-interval").value());

    std::string cancelling {xml_node.attribute("cancel").value()};
    config->cancelling = cancelling == "true" ? true : false;

    return std::static_pointer_cast<AgentConfig>(config);
}

AgentConfigPtr ConfigReader::configureMarketWatcher(int id, pugi::xml_node& xml_node, std::string& addr, std::unordered_map<std::string, std::string>& exchange_addrs)
{
    MarketWatcherConfigPtr config = std::make_shared<MarketWatcherConfig>();
    config->agent_id = id;
    config->addr = addr;
    config->type = AgentType::MARKET_WATCHER;

    config->exchange_name = std::string{xml_node.attribute("exchange").value()};
    config->exchange_addr = exchange_addrs.at(config->exchange_name);
    
    config->ticker = std::string{xml_node.attribute("ticker").value()};

    std::cout << "Configuring Market Watcher: Exchange=" << config->exchange_name 
          << ", Addr=" << config->exchange_addr 
          << ", Ticker=" << config->ticker << std::endl;

    return std::static_pointer_cast<AgentConfig>(config);
}

AgentConfigPtr ConfigReader::configureOrderInjector(int id, pugi::xml_node& xml_node, std::string& addr, std::unordered_map<std::string, std::string>& exchange_addrs)
{
    OrderInjectorConfigPtr config = std::make_shared<OrderInjectorConfig>();
    config->agent_id = id;
    config->addr = addr;
    config->type = AgentType::ORDER_INJECTOR;

    config->exchange_name = std::string{xml_node.attribute("exchange").value()};
    config->exchange_addr = exchange_addrs.at(config->exchange_name);
    
    config->ticker = std::string{xml_node.attribute("ticker").value()};

    // Supply & Demand Values 
    config->supply_min_low = xml_node.attribute("supply_min_low").as_int(0);
    config->supply_min_high = xml_node.attribute("supply_min_high").as_int(100);
    config->supply_max_low = xml_node.attribute("supply_max_low").as_int(100);
    config->supply_max_high = xml_node.attribute("supply_max_high").as_int(200);
    config->demand_min_low = xml_node.attribute("demand_min_low").as_int(0);
    config->demand_min_high = xml_node.attribute("demand_min_high").as_int(100);
    config->demand_max_low = xml_node.attribute("demand_max_low").as_int(100);
    config->demand_max_high = xml_node.attribute("demand_max_high").as_int(200);

    // Step mode configuration
    std::string step_mode = std::string{xml_node.attribute("step_mode").value()};
    if (step_mode == "fixed" || step_mode == "jittered" || step_mode == "random" || step_mode == "drip-poisson") 
    {
        config->step_mode = step_mode;
    } 
    else 
    {
        std::cerr << "Invalid step_mode: " << step_mode << ". Defaulting to 'fixed'.\n";
        config->step_mode = "fixed"; // Default mode from Python
    }

    // Time mode configuration (periodic, drip-fixed, drip-jitter, drip-poisson)
    std::string time_mode = std::string{xml_node.attribute("time_mode").value()};
    if (time_mode == "periodic" || time_mode == "drip-fixed" || time_mode == "drip-jitter" || time_mode == "drip-poisson") 
    {
        config->time_mode = time_mode;
    } 
    else 
    {
        std::cerr << "Invalid time_mode: " << time_mode << ". Defaulting to 'periodic'.\n";
        config->time_mode = "periodic"; // Default from Python
    }

    // Input File vs. Offset Logic (Matches Python config)
    bool use_input_file = xml_node.attribute("use_input_file").as_bool(false);
    bool use_offset = xml_node.attribute("use_offset").as_bool(false);

    if (use_input_file) 
    {
        config->use_input_file = true;
        config->input_file = std::string{xml_node.attribute("input_file").value()};
        config->use_offset = false;  // Ensure offset is disabled when input file is used
        std::cout << "Using input file for order schedule: " << config->input_file << std::endl;
    } 
    else if (use_offset) 
    {
        config->use_input_file = false;
        config->use_offset = true;
        std::cout << "Using offset function for order schedule." << std::endl;
    } 
    else 
    {
        config->use_input_file = false;
        config->use_offset = false;
        std::cout << "Using default static order scheduling." << std::endl;
    }

    // Interval 
    config->interval = xml_node.attribute("interval").as_int(1); // Default interval value

    std::cout << "Configuring Order Injector: " 
          << "Exchange=" << config->exchange_name 
          << ", Addr=" << config->exchange_addr 
          << ", Ticker=" << config->ticker 
          << ", SupplyMinLow=" << config->supply_min_low 
          << ", SupplyMinHigh=" << config->supply_min_high 
          << ", SupplyMaxLow=" << config->supply_max_low 
          << ", SupplyMaxHigh=" << config->supply_max_high 
          << ", DemandMinLow=" << config->demand_min_low 
          << ", DemandMinHigh=" << config->demand_min_high 
          << ", DemandMaxLow=" << config->demand_max_low 
          << ", DemandMaxHigh=" << config->demand_max_high 
          << ", StepMode=" << config->step_mode 
          << ", TimeMode=" << config->time_mode 
          << ", UseInputFile=" << (config->use_input_file ? "true" : "false") 
          << ", UseOffset=" << (config->use_offset ? "true" : "false") 
          << ", Interval=" << config->interval;

    return std::static_pointer_cast<AgentConfig>(config);
}

AgentConfigPtr ConfigReader::configureTraderZIP(int id, pugi::xml_node& xml_node, std::string& addr, std::unordered_map<std::string, std::string>& exchange_addrs)
{
    ZIPConfigPtr config = std::make_shared<ZIPConfig>();
    config->agent_id = id;

    config->type = AgentType::TRADER_ZIP;

    config->addr = addr;
    config->exchange_name = std::string{xml_node.attribute("exchange").value()};
    config->exchange_addr = exchange_addrs.at(config->exchange_name);
    config->limit = std::atoi(xml_node.attribute("limit").value());
    config->delay = std::atoi(xml_node.attribute("delay").value());
    config->ticker = std::string{xml_node.attribute("ticker").value()};

    std::string cancelling {xml_node.attribute("cancel").value()};
    config->cancelling = cancelling == "true" ? true : false;
    
    std::string side {xml_node.attribute("side").value()};
    if (side == "buy") config->side = Order::Side::BID;
    else if (side == "sell") config->side = Order::Side::ASK;

    config->min_margin = std::stod(xml_node.attribute("min-margin").value());
    std::cout << "trade: " << xml_node.attribute("trade-interval").value() << "\n";
    config->trade_interval = std::stoul(xml_node.attribute("trade-interval").value());
    std::cout << "liquidity: " << xml_node.attribute("liquidity-interval").value() << "\n";
    config->liquidity_interval = std::stoul(xml_node.attribute("liquidity-interval").value());


    return std::static_pointer_cast<AgentConfig>(config);

}

/** Reading and configuring trader configs from markets.csv. */

AgentConfigPtr ConfigReader::configureTraderFromCSV(int id, const std::string& addr, 
    const std::string& exchange, 
    const std::string& ticker, 
    AgentType trader_type, 
    const std::string& side, 
    const std::unordered_map<std::string, std::string>& exchange_addrs_map, const std::string& trader_string_name)
{
    TraderConfigPtr trader_config = std::make_shared<TraderConfig>();
    trader_config->agent_id = id;
    trader_config->addr = addr;
    trader_config->type = trader_type;
    trader_config->exchange_name = exchange;

    // Dynamically extract `exchange_addr` from `exchange_addrs_map` (DEFINED IN XML). 
    if (exchange_addrs_map.find(exchange) == exchange_addrs_map.end()) {
        throw std::runtime_error("Exchange address not found for " + exchange);
    }
    trader_config->exchange_addr = exchange_addrs_map.at(exchange);

    trader_config->ticker = ticker;
    //trader_config->limit = 50;  // Default limit value
    trader_config->delay = 0;   // Default delay value
    trader_config->trade_interval = 1; // Default interval value
    trader_config->cancelling = false; // Default

    // Assign Name Based on ID. 
    //trader_config->name = "Trader_" + std::to_string(id);
    trader_config->name = trader_string_name + ((side == "buy") ? "_Buyer" : "_Seller");

    // Set buy or sell side for each trader. 
    trader_config->side = (side == "buy") ? Order::Side::BID : Order::Side::ASK;

    // Assign a different limit price per trader - DEBUG TO TEST PROFITABILIITY BY TESTING RANDOM LIMIT PRICES
    std::uniform_int_distribution<int> dist(50, 70); // Range of limit prices
    static std::random_device rd; // Random seed
    static std::mt19937 gen(rd()); // Random number generator
    trader_config->limit = dist(gen);

    // DEBUG - Print Trader Configuration
    std::cout << "Configuring CSV Trader: ID=" << trader_config->agent_id
              << ", Name=" << trader_config->name
              << ", Exchange=" << trader_config->exchange_name
              << ", Exchange Addr=" << trader_config->exchange_addr
              << ", Ticker=" << trader_config->ticker
              << ", Limit=" << trader_config->limit
              << ", Trade Interval=" << trader_config->trade_interval
              << ", Delay=" << trader_config->delay
              << ", Side=" << side
              << "\n";
    
    return std::static_pointer_cast<AgentConfig>(trader_config);
}

/** Configure ZIP Trader from CSV specifically. */
AgentConfigPtr ConfigReader::configureTraderZIPFromCSV(
    int id, 
    const std::string& addr,
    const std::string& exchange,
    const std::string& ticker,
    AgentType trader_type,
    const std::string& side,
    const std::unordered_map<std::string, std::string>& exchange_addrs_map, 
    const std::string& trader_string_name)
{ 
    ZIPConfigPtr zip_config = std::make_shared<ZIPConfig>();
    zip_config->agent_id = id;
    zip_config->addr = addr;
    zip_config->type = trader_type;
    zip_config->exchange_name = exchange;

    if (exchange_addrs_map.find(exchange) == exchange_addrs_map.end()) {
        throw std::runtime_error("Exchange address not found for " + exchange);
    }

    zip_config->exchange_addr = exchange_addrs_map.at(exchange);
    zip_config->ticker = ticker;

    // Set default values

    std::uniform_int_distribution<int> dist(50, 70); // Range of limit prices
    static std::random_device rd; // Random seed
    static std::mt19937 gen(rd()); // Random number generator
    zip_config->limit = dist(gen);

    //zip_config->limit = 50; 
    zip_config->delay = 0;
    zip_config->trade_interval = 1;
    zip_config->cancelling = false;

    std::string cancelling {""};
    zip_config->side = (side == "buy") ? Order::Side::BID : Order::Side::ASK;

    // ZIP specific parameters 
    zip_config->min_margin = 0.01; // Default minimum margin value
    zip_config->name = trader_string_name + ((side == "buy") ? "_Buyer" : "_Seller");
    zip_config->liquidity_interval = 1; // Default liquidity interval value

    std::cout << "Configuring CSV ZIP Trader: ID=" << zip_config->agent_id
              << ", Name=" << zip_config->name
              << ", Exchange=" << zip_config->exchange_name
              << ", Exchange Addr=" << zip_config->exchange_addr
              << ", Ticker=" << zip_config->ticker
              << ", Limit=" << zip_config->limit
              << ", Trade Interval=" << zip_config->trade_interval
              << ", Delay=" << zip_config->delay
              << ", Side=" << zip_config->side
              << ", Min Margin=" << zip_config->min_margin
                << ", Liquidity Interval=" << zip_config->liquidity_interval
              << "\n";

    return std::static_pointer_cast<AgentConfig>(zip_config);

}
    

SimulationConfigPtr ConfigReader::readConfigFromCSV(const std::string& filepath, 
    const std::unordered_map<std::string, std::string>& exchange_addrs_map, int& agent_id, const std::string& default_exchange_name, const std::string& default_ticker)
{
    std::ifstream file(filepath); // Open CSV file
    if (!file.is_open()) // Check if file is open
    {
        throw std::runtime_error("Failed to open CSV file: " + filepath);
    }

    std::vector<AgentConfigPtr> trader_configs;
    std::vector<ExchangeConfigPtr> exchange_configs;
    std::vector<AgentConfigPtr> watcher_configs;
    std::vector<AgentConfigPtr> injector_configs;

    if (exchange_addrs_map.empty()) { // Check if exchange addresses map is empty
        throw std::runtime_error("No exchange addresses found in XML.");
    }

    int port = 8100; // Start assigning trader ports from 8100

    // These are the expected trader types (exactly 10 values).
    std::vector<std::string> trader_types = {"zic", "shvr", "vwap", "bb", "macd", "obvd", "obvvwap", "rsi", "rsibb", "zip"};
    std::unordered_map<std::string, AgentType> agent_type_map = {
        {"zic", AgentType::TRADER_ZIC}, 
        {"shvr", AgentType::TRADER_SHVR},
        {"vwap", AgentType::TRADER_VWAP},
        {"bb", AgentType::TRADER_BOLLINGER_BANDS},
        {"macd", AgentType::TRADER_MACD},
        {"obvd", AgentType::TRADER_OBV_DELTA},
        {"obvvwap", AgentType::TRADER_OBV_VWAP},
        {"rsi", AgentType::TRADER_RSI},
        {"rsibb", AgentType::TRADER_RSI_BB}, 
        {"zip", AgentType::TRADER_ZIP}
    };

    // Use default exchange name and ticker from XML - WORKS ONLY FOR ONE EXCHANGE; CHANGE FOR ARBITRAGE. 
    if (default_exchange_name.empty() || default_ticker.empty()) {
        throw std::runtime_error("No default exchange or ticker defined in XML.");
    }

    std::string line;
    while (std::getline(file, line)) // Read each line from the CSV file
    {
        std::istringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ss, token, ',')) // Split each line by comma
        {
            tokens.push_back(token);
        }

        // Validate exactly 10 agents (tokens). 
        if (tokens.size() != 10) {
            throw std::runtime_error("Invalid CSV format: each line must contain exactly 10 comma-separated values.");
        }

        // Process each token
        for (size_t index = 0; index < tokens.size(); ++index)
        {
            int count = std::stoi(tokens[index]); // Convert token to integer

            // Validate that each value is between 0 and 5. (0-5 buyers + sellers per agent type)
            if (count < 0 || count > 5) {
                throw std::runtime_error("Invalid value in CSV: each value must be between 0 and 5.");
            }

            std::string trader_string_name = trader_types[index]; // Get trader type from index
            AgentType type = agent_type_map[trader_string_name]; // Get agent type from trader type

            // For each count, create both buyer and seller agents.
            for (int i = 0; i < count; ++i)
            {
                for (const std::string& side : {"buy", "sell"}) // Create buyer and seller agents
                {   
                    std::cout << "Assigning trader " << agent_id << " to port " << port << std::endl;
                    std::cout << "Trader name " << trader_string_name << " with side " << side << std::endl;
                    std::string addr = "127.0.0.1:" + std::to_string(port++);
                    std::function<AgentConfigPtr(void)> configFunc =
                    (type == AgentType::TRADER_ZIP)
                    ? std::function<AgentConfigPtr(void)>([&]() -> AgentConfigPtr {
                        return configureTraderZIPFromCSV(
                            agent_id++, addr, default_exchange_name, default_ticker,
                            type, side, exchange_addrs_map, trader_string_name);
                    })
                    : std::function<AgentConfigPtr(void)>([&]() -> AgentConfigPtr {
                        return configureTraderFromCSV(
                            agent_id++, addr, default_exchange_name, default_ticker,
                            type, side, exchange_addrs_map, trader_string_name);
                    });
                trader_configs.push_back(configFunc());
                }
            }
        }
    }

    file.close();
    return std::make_shared<SimulationConfig>(1, 30, exchange_configs, trader_configs, watcher_configs, injector_configs);
}
