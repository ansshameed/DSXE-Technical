#include <iostream>
#include <algorithm>
#include <chrono>

#include "order.hpp"
#include "orderbook.hpp"

#include <boost/serialization/export.hpp>

BOOST_CLASS_EXPORT(MarketData);

void OrderBook::addOrder(LimitOrderPtr order)
{
    if (order->side == Order::Side::BID)
    {
        bids_.push(order);
        bids_volume_ += order->remaining_quantity;
        bids_sizes_[order->price] += order->remaining_quantity;
    }
    else
    {
        asks_.push(order);
        asks_volume_ += order->remaining_quantity;
        asks_sizes_[order->price] += order->remaining_quantity;
    }
    ++order_count_;
}

std::optional<LimitOrderPtr> OrderBook::removeOrder(int order_id, Order::Side side)
{
    if (side == Order::Side::BID)
    {
        std::optional<LimitOrderPtr> order = bids_.remove(order_id);
        if (order.has_value())
        {
            bids_volume_ -= order.value()->remaining_quantity;
            bids_sizes_[order.value()->price] -= order.value()->remaining_quantity;
            --order_count_;
        }
        return order;
    }
    else
    {
        std::optional<LimitOrderPtr> order = asks_.remove(order_id);
        if (order.has_value())
        {
            asks_volume_ -= order.value()->remaining_quantity;
            asks_sizes_[order.value()->price] -= order.value()->remaining_quantity;
            --order_count_;
        }
        return order;
    }
}

void OrderBook::updateOrderWithTrade(OrderPtr order, TradePtr trade)
{
    // Update average price executed
    order->avg_price = ((order->cumulative_quantity * order->avg_price) + (trade->quantity * trade->price)) / (order->cumulative_quantity + trade->quantity);

    // Update quantities
    order->cumulative_quantity += trade->quantity;
    order->remaining_quantity -= trade->quantity;

    // Update order status
    if (order->remaining_quantity == 0)
    {
        order->setStatus(Order::Status::FILLED);
    }
    else
    {
        order->setStatus(Order::Status::PARTIALLY_FILLED);
    }
}

std::optional<LimitOrderPtr> OrderBook::bestBid() 
{
    if (bids_.empty())
    {
        return std::nullopt;
    }
    return bids_.top();
}

int OrderBook::bestBidSize()
{
    std::optional<LimitOrderPtr> best_bid = bestBid();
    if (best_bid.has_value())
    {
        return bids_sizes_.at(best_bid.value()->price);
    }
    else 
    {
        return 0;
    }
}

std::optional<LimitOrderPtr> OrderBook::bestAsk() 
{
    if (asks_.empty())
    {
        return std::nullopt;
    }
    return asks_.top();
}

int OrderBook::bestAskSize()
{
    std::optional<LimitOrderPtr> best_ask = bestAsk();
    if (best_ask.has_value())
    {
        return asks_sizes_.at(best_ask.value()->price);
    }
    else 
    {
        return 0;
    }
}

void OrderBook::popBestBid()
{
    if (!bids_.empty())
    {
        bids_volume_ -= bids_.top()->remaining_quantity;
        std::cout << bids_volume_ << "\n";
        bids_sizes_[bids_.top()->price] -= bids_.top()->remaining_quantity;
        bids_.pop();
        --order_count_;
    }
}

void OrderBook::popBestAsk()
{
    if (!asks_.empty())
    {
        asks_volume_ -= asks_.top()->remaining_quantity;
        std::cout << asks_volume_ << "\n";
        asks_sizes_[asks_.top()->price] -= asks_.top()->remaining_quantity;
        asks_.pop();
        --order_count_;
    }
}

bool OrderBook::contains(int order_id, Order::Side side)
{
    if (side == Order::Side::BID)
    {
        return bids_.find(order_id).has_value();
    }
    else
    {
        return asks_.find(order_id).has_value();
    }
}

void OrderBook::updateRollingWindow(double high_price, double low_price)
{
    high_prices_.push_back(high_price);
    low_prices_.push_back(low_price);
    if (high_prices_.size() > lookback_period_)
    {
        high_prices_.pop_front();
        low_prices_.pop_front();
    }
}

std::optional<double> OrderBook::calculateHigh()
{
    if (high_prices_.empty()) return std::nullopt;
    return *std::max_element(high_prices_.begin(), high_prices_.end());
}

std::optional<double> OrderBook::calculateLow()
{
    if (low_prices_.empty()) return std::nullopt;
    return *std::min_element(low_prices_.begin(), low_prices_.end());
}

void OrderBook::logTrade(TradePtr trade)
{
    std::cout << "Logging trade: Price = " << trade->price << ", Quantity = " << trade->quantity << ", Timestamp = " << trade->timestamp << "\n";

    // Compute time difference using previous timestamp (for LOB)
    if (last_trade_.has_value())
    {
        time_diff_ = trade->timestamp - last_trade_.value()->timestamp;
        std::cout << "Last trade timestamp: " << last_trade_.value()->timestamp << "\n"; // Debug statement for last trade timestamp
        std::cout << "Time difference between current and last trade: " << time_diff_ << " nanoseconds\n"; // Debug statement for time difference
    } 
    else 
    { 
        time_diff_ = 0; // For first trade the time diff would be set to 0
    }

    // Debug statement to print last trade details
    if (last_trade_.has_value())
    {
        std::cout << "Last trade details: Price = " << last_trade_.value()->price 
                  << ", Quantity = " << last_trade_.value()->quantity 
                  << ", Timestamp = " << last_trade_.value()->timestamp << "\n";
    }

    last_trade_ = trade;
    updateRollingWindow(trade->price, trade->price);
    trade_high_ = calculateHigh();
    trade_low_ = calculateLow();
    trade_volume_ += trade->quantity;
    ++trade_count_;

    // Print updated high and low prices
    std::cout << "Updated High Price: " << trade_high_.value() << ", Updated Low Price: " << trade_low_.value() << "\n";
}

double OrderBook::getTotalBidVolume()
{
    return bids_volume_; 
}

double OrderBook::getTotalAskVolume() 
{
    return asks_volume_;
}

// Mid Price = Average of best bid price and best ask price in the LOB snapshot of the current time
double OrderBook::calculateMidPrice()  
{ 
    auto best_bid = bestBid();
    auto best_ask = bestAsk();

    if (!best_bid.has_value() || !best_ask.has_value()) // If there are no bids or asks in the order book
    {
        return -1;
    }

    return (best_bid.value()->price + best_ask.value()->price) / 2; // Calculate mid price as average of best bid and best ask prices
} 

// Micro Price = Weighted average of best bid and best ask prices in the LOB snapshot of the current time
double OrderBook::calculateMicroPrice() 
{ 
    auto best_bid = bestBid(); 
    auto best_ask = bestAsk(); 

    if (!best_bid.has_value() || !best_ask.has_value() || bestBidSize() == 0 || bestAskSize() == 0) // If there are no bids or asks in the order book or the size of the best bid or best ask is zero
    {
        return -1; 
    }
    
    double best_bid_price = best_bid.value()->price;
    double best_ask_price = best_ask.value()->price;
    int best_bid_size = bestBidSize();
    int best_ask_size = bestAskSize(); 

    return (best_bid_price * best_ask_size + best_ask_price * best_bid_size) / (best_bid_size + best_ask_size); // Calculate micro price as weighted average of best bid and best ask prices
}

int OrderBook::getAggressingSide(Order::Side aggressing_side)
{
    return (aggressing_side == Order::Side::BID) ? 0 : 1; // Return 0 if BID, 1 if ASK
}

double OrderBook::calculateImbalance() 
{   
    double qB = getTotalBidVolume();
    double qA = getTotalAskVolume();
    if (qB == 0 && qA == 0) // If there are no bids or asks in the order book - avoids division by 0
    {
        return 0;
    }
    return ((qB - qA) / (qB + qA)); // Calculate imbalance as difference between bid and ask volumes divided by sum of bid and ask volumes
}

double OrderBook::calculateSpread() 
{   
    auto best_bid = bestBid();
    auto best_ask = bestAsk();

    // Ensure both best bid and best ask are valid
    if (!best_bid.has_value() || !best_ask.has_value()) {
        return -1; // Indicate invalid spread when one side is empty
    }

    // Ensure spread is calculated correctly
    double spread = best_ask.value()->price - best_bid.value()->price;

    // Return spread
    return spread >= 0 ? spread : -1; // Ensure spread is never negative
}

MarketDataPtr OrderBook::getLiveMarketData(Order::Side aggressing_side)
{
    MarketDataPtr data = std::make_shared<MarketData>();
    data->ticker = ticker_;
    data->best_bid = bestBid().has_value() ? bestBid().value()->price : -1;
    data->best_ask = bestAsk().has_value() ? bestAsk().value()->price : -1;
    data->best_bid_size = bestBidSize();
    data->best_ask_size = bestAskSize();

    data->asks_volume = asks_volume_;
    data->bids_volume = bids_volume_;
    data->asks_count = asks_.size();
    data->bids_count = bids_.size();

    data->last_price_traded = last_trade_.has_value() ? last_trade_.value()->price : -1;
    data->last_quantity_traded = last_trade_.has_value() ? last_trade_.value()->quantity : 0;

    data->high_price = trade_high_.has_value() ? trade_high_.value() : -1;
    data->low_price = trade_low_.has_value() ? trade_low_.value() : -1;

    double current_volume_traded = trade_volume_;
    if (trade_count_ <= 1) {
        data->volume_per_tick = current_volume_traded;  // First tick takes full volume
    } else {
        data->volume_per_tick = std::max(0.0, current_volume_traded - previous_volume_traded_);
    }
    previous_volume_traded_ = current_volume_traded; // Update previous volume 


    data->cumulative_volume_traded = trade_volume_;
    data->trades_count = trade_count_;

    data->timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    // Additionals for DT 
    data->mid_price = calculateMidPrice();
    data->micro_price = calculateMicroPrice();
    data->side = getAggressingSide(aggressing_side);
    data->imbalance = calculateImbalance();
    data->spread = calculateSpread();
    data->time_diff = time_diff_;
    data->total_volume = data->asks_volume + data->bids_volume;
  
    return data;
} 