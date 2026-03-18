#pragma once

#include <list>
#include <exception>
#include <format>

#include "OrderType.h"
#include "Side.h"
#include "Usings.h"
#include "Constants.h"

#ifndef ORDERBOOK_ORDER_H
#define ORDERBOOK_ORDER_H

#endif //ORDERBOOK_ORDER_H

class Order {
public:
    Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
        : orderType_{ orderType }
        , orderId_{ orderId }
        , side_{ side }
        , price_{ price }
        // Quantity has three variables, initial, quantity remaining and filled
        , initialQuantity_{ quantity }
        , remainingQuantity_{ quantity }
    { }

    Order(OrderType orderType, OrderId orderId, Side side, Quantity quantity)
        : Order(OrderType::Market, orderId, side, Constants::InvalidPrice, quantity)
    { }

    OrderType GetOrderType() const { return orderType_; }
    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    Quantity GetInitialQuantity() const { return initialQuantity_; }
    Quantity GetRemainingQuantity() const { return remainingQuantity_; }
    // intitialQuantity_ and remainingQuantity_ are private so we must use the get functions to get value
    Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }

    // Return boolean IF remaining quantity of order is 0 then IsFilled == TRUE
    bool IsFilled() const { return GetRemainingQuantity() == 0; }

    // Filling the order with quantity causes remainingQuantity - quantity. If quantity is more than remaining then throw error
    void Fill(Quantity quantity) {
        if (quantity > GetRemainingQuantity())
            throw std::logic_error(std::format("Quantity ({}) cannot be filled for more than its remaining quantity.", GetOrderId()));

        remainingQuantity_ -= quantity;
    }

    void ToGoodTillCancel(Price price) {
        if (GetOrderType() != OrderType::Market)
            throw std::logic_error(std::format("Order ({}) cannot have its price adjusted, only market orders can.", GetOrderId()));

        price_ = price;
        orderType_ = OrderType::GoodTillCancel;
    }

private:
    OrderType orderType_;
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity initialQuantity_;
    Quantity remainingQuantity_;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;