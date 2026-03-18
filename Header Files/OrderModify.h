#pragma once

#include "Order.h"

#ifndef ORDERBOOK_ORDERMODIFY_H
#define ORDERBOOK_ORDERMODIFY_H

#endif //ORDERBOOK_ORDERMODIFY_H

// Cancel order requires orderId
// Add order requires order
// Modify order requires order as this is simple cancel order and add new order
class OrderModify {
public:
    OrderModify(OrderId orderId, Side side, Price price, Quantity quantity)
        : orderId_{ orderId }
    , price_{ price }
    , side_{ side }
    , quantity_{ quantity }
    {}

    OrderId GetOrderId() const { return orderId_; }
    Price GetPrice() const { return price_; }
    Side GetSide() const { return side_; }
    Quantity GetQuantity() const { return quantity_; }

    // A pointer is created pointing the Order. This allows us to pass the pointer to memory through functions instead of copying the order itself through functions
    OrderPointer ToOrderPointer(OrderType type) const {
        return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(), GetQuantity());
    }

private:
    OrderId orderId_;
    Price price_;
    Side side_;
    Quantity quantity_;
};