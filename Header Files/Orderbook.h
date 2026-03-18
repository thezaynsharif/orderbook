#pragma once

#include <map>
#include <unordered_map>
#include <thread>

#include <condition_variable>
#include <mutex>

#include "Usings.h"
#include "Order.h"
#include "OrderModify.h"
#include "OrderbookLevelInfos.h"
#include "Trade.h"


#ifndef ORDERBOOK_ORDERBOOK_H
#define ORDERBOOK_ORDERBOOK_H

#endif //ORDERBOOK_ORDERBOOK_H


class Orderbook {
private:

    //We want to store a record of the order entry location
    struct OrderEntry {
        // Store the location of the actual Order object
        OrderPointer order_{ nullptr };
        // Iterator points to the exact node in the price level. Allows for O(1) removal
        OrderPointers::iterator location_;
    };

    // We need to know the quantity at given price
    struct LevelData {
        Quantity quantity_{ };
        Quantity count_{ };

        // Level Data can be impacted when an order is added, removed or matched. Full match (We remove order as no quantity left) or Partial match (We update the quantity)
        enum class Action {
            Add,
            Remove,
            Match,
        };
    };

    // Metadata to store information about specific levels in the orderbook
    // Price against LevelData struct
    std::unordered_map<Price, LevelData> data_;
    // Create a map where key is price and value is the OrderPointers sorting bids with highest price first and asks with lowest price first
    // bids_.begin() gives best bid
    // asks_.begin() gives best ask
    std::map<Price, OrderPointers, std::greater<Price>> bids_;
    std::map<Price, OrderPointers, std::less<Price>> asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;

    mutable std::mutex ordersMutex_;
    std::thread ordersPruneThread_;
    std::condition_variable shutdownConditionVariable_;
    std::atomic<bool> shutdown_{ false };

    void PruneGoodForDayOrders();

    void CancelOrders(OrderIds orderIds);
    void CancelOrderInternal(OrderId orderId);

    void OnOrderCancelled(OrderPointer order);
    void OnOrderAdded(OrderPointer order);
    void OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled);
    void UpdateLevelData(Price price, Quantity quantity, LevelData::Action action);

    bool CanFullyFill(Side side, Price price, Quantity quantity) const;
    // Check if the order placed can be matched with an order in the orderbook
    bool CanMatch(Side side, Price price) const;
    // MatchOrders() is a function of type Trades line 152
    Trades MatchOrders();

public:
    Orderbook();
    Orderbook(const Orderbook&) = delete;
    void operator=(const Orderbook&) = delete;
    Orderbook(Orderbook&&) = delete;
    void operator=(Orderbook&&) = delete;
    ~Orderbook();

    Trades AddOrder(OrderPointer order); // Add order function
    void CancelOrder(OrderId orderId);
    // Modifying the order is just a cancel order and add order call
    Trades MatchOrder(OrderModify order);

    // Function Size() that returns the size of the orders_ table
    std::size_t Size() const;
    OrderbookLevelInfos GetOrderInfo() const;
};