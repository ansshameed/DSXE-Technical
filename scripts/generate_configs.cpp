#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <set>
#include <map> 
#include <unordered_set>
#include <cstdlib> // For system() function

int main() {
    // Base distributions (summing to 20 per side; 40 total)
    std::vector<std::vector<int>> distributions = {
        // 1. Equal representation baseline
        {2, 2, 2, 2, 2, 2, 2, 2, 2, 2},  // Even distribution of all trader types
        
        // 2. All legacy traders with ZIP
        {5, 5, 5, 0, 0, 0, 0, 0, 0, 5},  // Pure legacy: ZIC, SHVR, VWAP with ZIP
        
        // 3. All technical indicators with ZIP
        {0, 0, 0, 4, 4, 4, 4, 2, 0, 2},  // Technical indicators with ZIP adaptation
        
        // 4. Momentum-focused technical mix
        {0, 0, 3, 5, 6, 0, 0, 6, 0, 0},  // VWAP, BB, MACD, RSI (momentum/trend focus)
        
        // 5. Volume-focused technical mix
        {0, 0, 0, 0, 0, 8, 8, 0, 0, 4},  // OBVD, OBVVWAP with ZIP (volume analysis)
        
        // 6. Legacy with price action technical
        {5, 5, 0, 4, 4, 0, 0, 2, 0, 0},  // ZIC, SHVR with BB, MACD, RSI (chart patterns)
        
        // 7. Legacy with volume technical and ZIP
        {5, 0, 5, 0, 0, 4, 4, 0, 0, 2},  // ZIC, VWAP with OBVD, OBVVWAP and ZIP
        
        // 8. Adaptive competition mix
        {7, 0, 0, 0, 0, 0, 0, 0, 0, 13}, // ZIC vs ZIP (classic simple vs adaptive)
        
        // 9. Complex technical integration
        {0, 0, 4, 4, 0, 0, 4, 0, 8, 0},  // VWAP, BB, OBVVWAP, RSIBB (multi-factor analysis)
        
        // 10. Combined oscillator strategies with ZIP
        {0, 0, 0, 0, 6, 0, 0, 6, 6, 2},  // MACD, RSI, RSIBB with ZIP adaptation
        
        // 11. Hybrid price-volume analysis
        {0, 4, 4, 4, 0, 4, 4, 0, 0, 0},  // SHVR, VWAP, BB, OBVD, OBVVWAP (multi-input)
        
        // 12. Research continuity
        {8, 8, 2, 2, 0, 0, 0, 0, 0, 0}  
    };

    // Configuration parameters
    int trials_per_configuration = 3;

    // Debug: Print the current working directory
    std::cout << "Writing to current directory: ";
    system("pwd");

    // Try multiple file paths
    std::string filename = "./markets.csv";
    std::ofstream markets_file(filename);
    
    if (!markets_file.is_open()) {
        std::cerr << "Failed to create " << filename << ", trying alternative path..." << std::endl;
        
        // Try an absolute path as fallback
        filename = "/app/build/markets.csv";
        markets_file.open(filename);
        
        if (!markets_file.is_open()) {
            std::cerr << "Failed to create " << filename << " as well!" << std::endl;
            
            // Create a simple test file as a last resort
            std::cout << "Creating a simple test file as fallback..." << std::endl;
            std::ofstream test_file("test_file.txt");
            if (test_file.is_open()) {
                test_file << "Testing file creation" << std::endl;
                test_file.close();
                std::cout << "Successfully created test_file.txt" << std::endl;
                system("ls -la");
            } else {
                std::cerr << "Failed to create even a simple test file!" << std::endl;
            }
            
            // Try using system command as fallback
            std::cout << "Trying to create markets.csv using echo command..." << std::endl;
            system("echo '2,2,2,2,2,2,2,2,2,2' > markets.csv");
            system("ls -la markets.csv");
            
            return 1;
        }
    }

    std::cout << "Successfully opened file for writing at: " << filename << std::endl;

    int config_count = 0;
    std::vector<std::vector<int>> all_permutations;

    // Use a simple configuration for testing if needed
    bool use_simple_config = false;
    if (use_simple_config) {
        std::vector<int> simple_config = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
        all_permutations.push_back(simple_config);
        config_count = 1;
    } else {
        // Normal processing of distributions
        for (size_t dist_index = 0; dist_index < distributions.size(); dist_index++) {
            auto dist = distributions[dist_index];
        
            // Print info about this distribution
            std::cout << "Distribution " << (dist_index+1) << ": ";
            for (int val : dist) std::cout << val << " ";
            std::cout << std::endl;
        
            // Count the occurrences of each value in the distribution
            std::map<int, int> value_counts;
            for (int val : dist) {
                value_counts[val]++;
            }
        
            // Calculate the total number of unique permutations using the formula
            int total_positions = dist.size();
            long long total_perms = 1;
            for (int i = 1; i <= total_positions; i++) {
                total_perms *= i;
            }
            for (const auto& pair : value_counts) {
                long long factorial = 1;
                for (int i = 1; i <= pair.second; i++) {
                    factorial *= i;
                }
                total_perms /= factorial;
            }
        
            std::cout << "Calculated " << total_perms << " unique permutations for distribution " << std::endl;
        
            std::set<std::vector<int>> unique_perms;
            
            // Start with the original distribution sorted to get all permutations
            std::vector<int> current_perm = dist;
            std::sort(current_perm.begin(), current_perm.end());
            
            // Add the first permutation
            unique_perms.insert(current_perm);
            
            // Use std::next_permutation to generate all permutations
            while (std::next_permutation(current_perm.begin(), current_perm.end())) {
                unique_perms.insert(current_perm);
            }
            
            if (unique_perms.size() != total_perms) {
                std::cerr << "Warning: Permutation count mismatch! Algorithm: " 
                        << unique_perms.size() << ", Formula: " << total_perms << std::endl;
            }
            
            // Add unique permutations collection
            for (const auto& perm : unique_perms) {
                all_permutations.push_back(perm);
                config_count++;
            }
        
            std::cout << "Generated " << unique_perms.size() << " unique permutations for distribution " 
                    << (dist_index+1) << std::endl;
        }
    }

    // Write all permutations to the CSV file
    for (const auto& perm : all_permutations) {
        for (size_t i = 0; i < perm.size(); i++) {
            markets_file << perm[i];
            if (i < perm.size() - 1) {
                markets_file << ",";
            }
        }
        markets_file << std::endl;
    }

    markets_file.close();
    std::cout << "Total unique configurations generated: " << config_count << std::endl;
    std::cout << "Total simulation runs planned: " << config_count * trials_per_configuration << std::endl;

    // Verify the file was created successfully
    std::cout << "Verifying markets.csv exists after creation:" << std::endl;
    system("ls -la markets.csv || echo 'File not found!'");
    
    // Backup the markets file
    system("echo 'Backup markets file' > markets.csv.backup");
    system("ls -la markets.csv.backup || echo 'Backup file not found!'");

    return 0;
}