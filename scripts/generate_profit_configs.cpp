#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>

int main() {
    // Open the output file
    std::ofstream market_file("markets_profits.csv");
    if (!market_file.is_open()) {
        std::cerr << "Failed to create markets_profits.csv" << std::endl;
        return 1;
    }

    // Define all 12 trader types
    std::vector<std::string> all_traders = {
        "zic", "shvr", "vwap", "bb", "macd", "obvd", "obvvwap", "rsi", "rsibb", "zip", "deeplstm", "deepxgb"
    };
    
    // Testing
    std::vector<std::string> custom_traders = {"deeplstm", "deepxgb"};
    std::vector<std::string> standard_traders(all_traders.begin(), all_traders.begin() + 10); // First 10 traders
    
    int total_configs = 0;
    
    // Generate balanced group tests (10 vs 10)
    std::cout << "Generating balanced group test configurations..." << std::endl;
    for (const auto& custom_trader : custom_traders) {
        for (const auto& trader_type : standard_traders) {
            // Initialise all traders to 0
            std::vector<int> config(12, 0);
            
            // Find the index of the standard trader
            int std_trader_idx = -1;
            for (size_t i = 0; i < standard_traders.size(); i++) {
                if (standard_traders[i] == trader_type) {
                    std_trader_idx = i;
                    break;
                }
            }
            
            if (std_trader_idx == -1) {
                std::cerr << "Error: Could not find trader type: " << trader_type << std::endl;
                continue;
            }
            
            // Set 10 traders for the standard trader type
            config[std_trader_idx] = 10;
            
            // Set 10 traders for the custom trader
            if (custom_trader == "deeplstm") {
                config[10] = 10;
            } else { // deepxgb
                config[11] = 10;
            }
            
            // Write the configuration to the file
            for (size_t i = 0; i < config.size(); i++) {
                market_file << config[i];
                if (i < config.size() - 1) {
                    market_file << ",";
                }
            }
            market_file << std::endl;
            total_configs++;
        }
    }
    
    // Generate one-to-many tests (1 vs 19)
    std::cout << "Generating one-to-many test configurations..." << std::endl;
    for (const auto& custom_trader : custom_traders) {
        for (const auto& trader_type : standard_traders) {
            // Initialise all traders to 0
            std::vector<int> config(12, 0);
            
            // Find the index of the standard trader
            int std_trader_idx = -1;
            for (size_t i = 0; i < standard_traders.size(); i++) {
                if (standard_traders[i] == trader_type) {
                    std_trader_idx = i;
                    break;
                }
            }
            
            if (std_trader_idx == -1) {
                std::cerr << "Error: Could not find trader type: " << trader_type << std::endl;
                continue;
            }
            
            // Set 19 traders for the standard trader type
            config[std_trader_idx] = 19;
            
            // Set 1 trader for the custom trader
            if (custom_trader == "deeplstm") {
                config[10] = 1;
            } else { // deepxgb
                config[11] = 1;
            }
            
            // Write the configuration to the file
            for (size_t i = 0; i < config.size(); i++) {
                market_file << config[i];
                if (i < config.size() - 1) {
                    market_file << ",";
                }
            }
            market_file << std::endl;
            total_configs++;
        }
    }
    
    market_file.close();
    
    std::cout << "Successfully generated " << total_configs << " configurations" << std::endl;
    std::cout << "Running 500 trials each = " << total_configs * 500 << " total simulation runs" << std::endl;
    
    // Verify the file was created
    std::cout << "Checking if file was created:" << std::endl;
    system("ls -la markets_profits.csv || echo 'File not found!'");
    
    return 0;
}