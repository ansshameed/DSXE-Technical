#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

int main() {
    std::ifstream file("/Users/ansshameed/Documents/year 3/DSXE/distributed-stock-exchange-environment/build/trades_NYSE_AAPL_2025-02-14T20:32:50.csv");
    if (!file.is_open()) {
        std::cerr << "Failed to open the file." << std::endl;
        return 1;
    }

    std::string line;
    int buyer_count = 0;
    int seller_count = 0;

    // Skip the header line
    std::getline(file, line);

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string id, ticker, quantity, price, timestamp, buyer_id, seller_id, agent_name, aggressing_order_id, resting_order_id, buyer_priv_value, seller_priv_value;

        std::getline(ss, id, ',');
        std::getline(ss, ticker, ',');
        std::getline(ss, quantity, ',');
        std::getline(ss, price, ',');
        std::getline(ss, timestamp, ',');
        std::getline(ss, buyer_id, ',');
        std::getline(ss, seller_id, ',');
        std::getline(ss, agent_name, ',');
        std::getline(ss, aggressing_order_id, ',');
        std::getline(ss, resting_order_id, ',');
        std::getline(ss, buyer_priv_value, ',');
        std::getline(ss, seller_priv_value, ',');

        if (buyer_id == "3") {
            buyer_count++;
        }
        if (seller_id == "7") {
            seller_count++;
        }
    }

    file.close();

    std::cout << "Buyer ID '3' count: " << buyer_count << std::endl;
    std::cout << "Seller ID '6' count: " << seller_count << std::endl;

    return 0;
}