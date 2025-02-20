#include "orderschedule.hpp" 
#include <fstream>
#include <sstream> 
#include <vector> 
#include <string> 
#include <stdexcept>
#include <iomanip> 
#include <chrono> 
#include <cmath> 
#include <limits> 

/** REPLICATING LOGIC FOR INPUT HISTORICAL DATA FROM TBSE.PY - DEEPTRADER. */

/** Parsing time string "HH:MM:SS" into seconds since midnight. */
static double parseTime(const std::string &time_string) 
{ 
    std::tm tm = {}; 
    std::istringstream ss(time_string);
    ss >> std::get_time(&tm, "%H:%M:%S");
    if (ss.fail()) 
    {
        throw std::invalid_argument("Invalid time format: " + time_string);
    }
    return tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec; // Return seconds since midnight
}

/** Reads CSV historical file and returns vector of pairs  */
// Normalised time [0, 1] scaled price offset 
std::vector<std::pair<double, int>> getOffsetEventList(const std::string &historical_data_file) 
{
    std::ifstream file(historical_data_file); 
    if (!file.is_open()) 
    {
        throw std::runtime_error("Failed to open the file: " + historical_data_file);
    }
    std::vector<std::pair<double, double>> raw_events; // (elapsed time, price offset)
    std::string line; 
    bool first_line = true; 
    double first_time = 0.0; 
    double min_price = std::numeric_limits<double>::max();
    double max_price = std::numeric_limits<double>::min();

    // Read each line (assumes CSV format: Date,Time,Open,High,Low,Close,Volume)
    while (std::getline(file,line)) { 
        if (line.empty()) continue;
        std::istringstream ss(line); 
        std::string date, time, open, high, low, close, volume;
        std::getline(ss, date, ',');
        std::getline(ss, time, ',');
        std::getline(ss, open, ',');
        std::getline(ss, high, ',');
        std::getline(ss, low, ',');
        std::getline(ss, close, ',');
        std::getline(ss, volume, ',');
        double price = std::stod(close); // Use close price

        double current_time = parseTime(time); // Parse time string into seconds since midnight
        if (first_line) 
        {
            first_time = current_time;
            first_line = false;
        }
        double elapsed_time = current_time - first_time; // Elapsed time since first data point
        raw_events.emplace_back(elapsed_time, price); // Add (elapsed time, price) pair to event list
        if (price < min_price) min_price = price; // Update min price
        if (price > max_price) max_price = price; // Update max price
    }
    file.close(); 

    if (raw_events.empty()) 
    {
        throw std::runtime_error("No data points found in historical file: " + historical_data_file);
    }

    double total_time = raw_events.back().first; // Total time elapsed
    double price_range = max_price - min_price; // Price range
    int scale_factor = 80; // Scale factor for price offset

    // Normalise and scale price offset
    std::vector<std::pair<double, int>> offset_events; // Normalised event list
    for (const auto &event : raw_events) 
    {
        double normalised_time = event.first / total_time; // Normalised time [0, 1]
        double normalised_price = (event.second - min_price) / price_range; // Normalised price [0, 1]
        if (normalised_price > 1.0) normalised_price = 1.0; // Cap normalised price at 1.0
        if (normalised_price < 0.0) normalised_price = 0.0; // Cap normalised price at 0.0
        int scaled_price = static_cast<int>(normalised_price * scale_factor); // Scale price offset
        offset_events.emplace_back(normalised_time, scaled_price); // Add (normalised time, scaled price) pair to event list
    }
    return offset_events; // Return normalised event list
} 

/** Dynamic offset function based on real-world data. Offset = how far through total time we are. */
// Given a time t (in seconds) and preempted offset events (plus total time), returns corresponding offset 
// Offset value = adjusts the baseline price range to simulate real-world price fluctuations
int realWorldScheduleOffset(double time, double total_time, const std::vector<std::pair<double, int>> &offset_events)
{ 
    double percent_elpased = time / total_time; // Percentage of time elapsed
    double offset = 0; 
    for (const auto &event : offset_events) // Iterate through offset events
    {
        if (percent_elpased <= event.first) // If time is less than or equal to event time
        {
            offset = event.second; // Set offset to event offset
            break; 
        }
    }
    return offset; 
}

/** Simple alternative offset function; IF NOT USING INPUT HISTORICAL DATA FILE. */
/** TAKEN FROM DEEPTRAER TBSE.PY - Dynamic offset using sine wave combined with a linear trend. */
int scheduleOffset(double time) 
{ 
    double pi2 = 2 * M_PI; 
    double c = M_PI * 3000; 
    double wavelength = time / c;
    double gradient = 100 * time / (c / pi2);
    double amplitude = 100 * time / (c / pi2);
    double offset = gradient + amplitude * sin(wavelength * time); 
    return static_cast<int>(std::round(offset)); 
}