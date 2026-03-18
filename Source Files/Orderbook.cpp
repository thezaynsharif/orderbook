#include "../Header Files/Orderbook.h"

#include <numeric>
#include <chrono>
#include <ctime>

void Orderbook::PruneGoodForDayOrders() {
    using namespace std::chrono;
    // At hour 16 (4pm)
    const auto end = hours(16);

    while (true) {
        const auto now = system_clock::now(); // Get the current time from the system clock (timestamp object, not date/time)
        const auto now_c = system_clock::to_time_t(now); // Converts the now time into time_t
        std::tm now_parts; // std::tm is a time structure holding calendar components (sec, min, hour, mday, mon, year)
        localtime_r(&now_c, &now_parts); // Converts now_c into local time and breaks into readable components

        // If the current hour is past 4pm (end time) then add one more day to the cut off
        // If your order is already past today's cut off then the next cut off is tomorrow
        if (now_parts.tm_hour >= end.count())
            now_parts.tm_mday += 1;

        // Set the time to prune to 4pm at min and sec zero
        now_parts.tm_hour = end.count();
        now_parts.tm_min = 0;
        now_parts.tm_sec = 0;

        // Now parts is now set to the next 4:00 PM cut off
        // mktime converts that calendar time to time_t, from time_t back into time_point
        auto next = system_clock::from_time_t(mktime(&now_parts)); // next = the exact clock time of the next 4pm prune
        auto till = next - now + milliseconds(100); // till is the duration from "right now" until the prune time. Add 100 milliseconds as a safety buffer

        // We have two threads that can interfere with the order book at the same time:
        // Main thread: Add, Cancel Modify orders
        // Prune thread: Sleeps until 4pm, wakes up, removes all GoodForDay orders
        // If two threads modify the same data at the same time, that is undefined behaviour and can cause bugs

        // Mutex is used as a "key" where if someone has it, everyone else must wait. Only one person can hold the key at a time

        // {...} creates a new scope inside the function. When you leave the {...} then everything inside is destroyed.
        // When we exit this part of the function the lock is destroyed and mutex is unlocked
        {
            std::unique_lock ordersLock{ ordersMutex_ }; // Lock the orderbook so nothing changes during this task

            // Condition variable hold the condition
            // If the program is being shutdown -> stop the prune thread
            // Or if it's not time to prune yet -> also stop
            if (shutdown_.load(std::memory_order_acquire) ||
                shutdownConditionVariable_.wait_for(ordersLock, till) == std::cv_status::no_timeout)
                return;
        }

        // Create an empty list to store the IDs of the orders to prune
        OrderIds orderIds;

        // Iterate over all outstanding orders and if the order is GoodForDay then we need to cancel it
        {
            // Lock the orderbook so nothing changes orders whilst reading it
            std::scoped_lock ordersLock{ ordersMutex_ };

            // Loop for every entry in orders_
            for (const auto& [_, entry] : orders_) {
                // entry contains the orderpointer and iterator so, order = OrderPointer, ignore the iterator
                const auto& [order, _] = entry;

                // If order type is not GoodForDay then skip this one
                if (order->GetOrderType() != OrderType::GoodForDay)
                    continue;

                // Push the OrderID to the orderIds list
                orderIds.push_back(order->GetOrderId());
            }
        }

        // Cancel the orders
        CancelOrders(orderIds);
    }
}

void Orderbook::CancelOrders(OrderIds orderIds) {
    std::scoped_lock ordersLock{ ordersMutex_ };

    for (const auto& orderId : orderIds) {
        CancelOrderInternal(orderId);
    }
}

void Orderbook::CancelOrderInternal(OrderId orderId) {
    // orders_ is the "fast lookup table"
    // if the orderId does not exist then return
    if (!orders_.contains(orderId))
        return;

    // Get the pointer to order info and the order node position from the orders_ table -> O(1) time
    const auto& [order, orderIterator] = orders_.at(orderId);

    // If order to cancel is Sell side
    if (order->GetSide() == Side::Sell) {
        // Get price from the order
        auto price = order->GetPrice();
        // Get the list of ask orders at price level
        auto& orders = asks_.at(price);
        // Erase the order from asks order level using orderIterator for O(1) removal
        orders.erase(orderIterator);
        // If the asks_ table is now empty, erase the order list from the orderbook
        if (orders.empty())
            asks_.erase(price);
    }
    // Else do the same as above but for Buy side
    else {
        auto price = order->GetPrice();
        auto& orders = bids_.at(price);
        orders.erase(orderIterator);
        if (orders.empty())
            bids_.erase(price);
    }

    OnOrderCancelled(order);
    orders_.erase(orderId);
}

void Orderbook::OnOrderCancelled(OrderPointer order) {
    UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
}

void Orderbook::OnOrderAdded(OrderPointer order) {
    UpdateLevelData(order->GetPrice(), order->GetInitialQuantity(), LevelData::Action::Add);
}

void Orderbook::OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled) {
    // If the order can be fully filled then the order count should be reduced by 1 in UpdateLevelData()
    // If it is just a match then the order count remains the same but the quantity is reduced
    UpdateLevelData(price, quantity, isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
}

void Orderbook::UpdateLevelData(Price price, Quantity quantity, LevelData::Action action) {
    auto& data = data_[price];

    // If order is removed then reduce order count by 1
    // If order is added then increase order count by 1
    // If order was matched then do nothing
    data.count_ += action == LevelData::Action::Remove ? -1 : action == LevelData::Action::Add ? 1 : 0;

    // If action is either remove or match then reduce the quantity being removed or matched
    if (action == LevelData::Action::Remove || action == LevelData::Action::Match) {
        data.quantity_ -= quantity;
    }
    else {
        data.quantity_ += quantity;
    }

    // If the count of orders is ZERO then remove this empty data from the level data
    if (data.count_ == 0)
        data_.erase(price);
}

bool Orderbook::CanFullyFill(Side side, Price price, Quantity quantity) const {
    // If the order can't match with some orders then return fals
    if (!CanMatch(side, price))
        return false;

    std::optional<Price> threshold;

    if (side == Side::Buy) {
        // We want to find the quantity of orders between the best ask and the ask the order is looking for
        const auto [askPrice, _] = *asks_.begin(); // Best ask price
        threshold = askPrice;
    }
    else {
        const auto [bidPrice, _] = *bids_.begin();
        threshold = bidPrice;
    }

    // Iterate through level data
    for (const auto& [levelPrice, levelData] : data_) {
        if (threshold.has_value() &&
            (side == Side::Buy && threshold.value() > levelPrice) ||
            (side == Side::Sell && threshold.value() < levelPrice))
            continue;

        // If the price level is greater than what we want to buy for then skip
        // If the price level is lower than what we want to sell for then skip
        if ((side == Side::Buy && levelPrice > price) ||
            (side == Side::Sell && levelPrice < price))
            continue;

        // If quantity remaining in order can be filled return true
        if (quantity <= levelData.quantity_)
            return true;

        // If quantity hasn't been fully matched yet, reduce order quantity and continue loop
        quantity -= levelData.quantity_;
    }

    return false;
}

// Check if the order placed can be matched with an order in the orderbook
bool Orderbook::CanMatch(Side side, Price price) const {
    // If the order is Buy
    if (side == Side::Buy) {
        // Order cannot be matched if the asks_ side is empty i.e. no ask orders
        if (asks_.empty())
            return false;

        // & takes address of the object
        // * dereference pointer so *asks_.begin() gives the first element of the map i.e. the best ask price
        // map element is a pair <Price, OrderPointers> so "const auto& [bestAsk, _]" will store the price in bestAsk and the list of orders in _.
        // If I am placing a Buy order at price, is my price high enough to match the cheapest seller?
        const auto& [bestAsk, _] = *asks_.begin();
        return price >= bestAsk;
    }
    // If the order is a Sell
    else {
        if (bids_.empty())
            return false;

        // Find the best bid price which is the highest bid
        // Is the Sellers price low enough to match the highest bid price on the orderbook?
        const auto& [bestBid, _] = *bids_.begin();
        return price <= bestBid;
    }
}

// MatchOrders() is a function of type Trades line 152
Trades Orderbook::MatchOrders() {
    // Create variable trades of type Trades. Pre allocate memory of size of the orderbook (max case if the trade filled all orders)
    Trades trades;
    trades.reserve(orders_.size());

    while (true) {
        // If there are no bid orders or no ask orders then exit as no trade can filled
        if (bids_.empty() || asks_.empty())
            break;

        // Get best bid price and store price in bidPrice and store list of bids in bids. Do same for asks.
        // bidPrice is the best price level and bids is the list (FIFO queue) of orders at that price. Same for asks
        auto& [bidPrice, bids] = *bids_.begin();
        auto& [askPrice, asks] = *asks_.begin();

        // If the highest price any buyer is willing to pay (bidPrice) is less than the lowest price any seller is willing to accept (askPrice) then no trade can be made
        // Example: best bid = 99 and best ask = 101. Nobody agrees on price -> no trade is made
        if (bidPrice < askPrice)
            break;

        // Loop while there are orders
        while (bids.size() && asks.size()) {
            // bid is the bid item at the front of the queue (FIFO)
            // ask is the ask item at the fron of the queue
            auto bid = bids.front();
            auto ask = asks.front();

            // Quantity of this trade will be whatever the minimum quantity is between our bid and ask orders.
            // Example: bid quantity = 3 and ask quantity = 2. We can only match 2 asks to the bid with 1 bid remaining to be filled.
            Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

            // Fill the bid and ask orders with the quantity. remaining quantity - quantity being filled.
            bid->Fill(quantity);
            ask->Fill(quantity);

            // Check if the bid order has been completely filled i.e. remaining quantity == 0
            // If so, pop order from queue and erase the order.
            if (bid->IsFilled()) {
                bids.pop_front();
                orders_.erase(bid->GetOrderId());
            }

            // Do the same for the ask order
            if (ask->IsFilled()) {
                asks.pop_front();
                orders_.erase(ask->GetOrderId());
            }

            // If the bids price level is now empty, remove it
            if (bids.empty())
                bids_.erase(bidPrice);

            // If the asks price level is now empty, remove it
            if (asks.empty())
                asks_.erase(askPrice);

            // Generate the trade object with the bid and ask orderId, price and quantity to be returned as part of this funtion
            trades.push_back(Trade{
                TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity},
                TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity}
            });

            OnOrderMatched(bid->GetPrice(), quantity, bid->IsFilled());
            OnOrderMatched(ask->GetPrice(), quantity, ask->IsFilled());
        }
    }

    // If the bid order is not empty and the order was a fill and kill THEN cancel it
    // FILL AND KILL: Fill as much of order then Kill order if rest cannot be filled.
    if (!bids_.empty()) {
        auto& [_, bids] = *bids_.begin();
        auto& order = bids.front();
        if (order->GetOrderType() == OrderType::FillAndKill)
            CancelOrder(order->GetOrderId());
    }

    // If the ask order is not empty and the order was a fill and kill THEN cancel it
    if (!asks_.empty()) {
        auto& [_, asks] = *asks_.begin();
        auto& order = asks.front();
        if (order->GetOrderType() == OrderType::FillAndKill)
            CancelOrder(order->GetOrderId());
    }

    return trades;
}

Orderbook::Orderbook() : ordersPruneThread_{ [this] { PruneGoodForDayOrders(); } } { }

Orderbook::~Orderbook() {
    shutdown_.store(true, std::memory_order_release);
    shutdownConditionVariable_.notify_one();
    ordersPruneThread_.join();
}

// Add order function
Trades Orderbook::AddOrder(OrderPointer order) {
    // If the orderId is already inside our orderbook then return empty trades (rejection)
    if (orders_.contains(order->GetOrderId()))
        return { };

    // MARKET ORDER type
    if (order->GetOrderType() == OrderType::Market) {
        // If looking to BUY and there are sellers on other side
        if (order->GetSide() == Side::Buy && !asks_.empty()) {
            // Find the worst ask price and leverage exisiting order type GoodTillCancel with price = worstAsk
            // We use the worst price so the market order is aggressive enough to match through all available price levels, not just the first level
            // EXAMPLE: If asks are
            // 101 (2 units)
            // 102 (4 units)
            // 105 (10 units)
            //
            // And a Market BUY order comes in for 6 units:
            // If we cap the price at 101 (best ask), it would only fill 2 units and stop.
            //
            // By using 105 (worst ask), the order can match 2 at 101 and 4 at 102,
            // allowing it to walk through multiple price level like a true market order.
            const auto& [worstAsk, _] = *asks_.rbegin();
            order->ToGoodTillCancel(worstAsk);
        }
        // If looking SELL and there are Buyers on the other side
        else if (order->GetSide() == Side::Sell && !bids_.empty()) {
            const auto& [worstBid, _] = *bids_.rbegin();
            order->ToGoodTillCancel(worstBid);
        }
        else
            return { };
    }

    // If the order is a FILL AND KILL and cannot be matched, THEN Kill the order
    if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
        return { };

    // If the order is FILL OR KILL and cannot be fully filled, THEN Kill the order
    if (order->GetOrderType() == OrderType::FillOrKill && !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity()))
        return { };

    // Iterator is used to store the exact position of node so we can erase later in O(1) time
    OrderPointers::iterator iterator;

    // 1. Put into the correct price level queue
    // 2. Capture an iterator pointing exactly to where the order was added
    // 3. Store the iterator in orders_ so you can cancel/modify quickly
    // 4. Run MathcOrders() to execute the trades

    // If the order being added is Buy side
    if (order->GetSide() == Side::Buy) {
        // Get the list of bids at price level
        // If price level exists -> return reference to list
        // If price level does not exist -> creates new empty list at price and return list
        auto& orders = bids_[order->GetPrice()];
        // Push order to back of the list (FIFO) ensuring time ordering
        orders.push_back(order);
        // Iterate through the list to end and store end node position in iterator
        iterator = std::next(orders.begin(), orders.size() - 1);
    }
    // ELSE do same on the Sell side (asks)
    else {
        auto& orders = asks_[order->GetPrice()];
        orders.push_back(order);
        iterator = std::next(orders.begin(), orders.size() - 1);
    }

    // Insert the orderId and pointers into orders_ table
    // orders_ table is the "fast lookup table" we created to allow for fast lookup and erasing
    orders_.insert({ order->GetOrderId(), OrderEntry{ order, iterator} });

    OnOrderAdded(order);

    // Once order is now added, execute and return the trades
    return MatchOrders();
};

// Cancel Order creates a lock on the orderbook to prevent other changes
// CancelOrderInternal is our cancel order logic
// We call the CancelOrderInternal() from both CancelOrder() and CancelOrders(), this means only one mutex is created for each request
// If instead we had CancelOrder having mutex and the logic, when we call CancelOrders() we will create and flush a new mutex for every order being cancelled. This is inefficient
void Orderbook::CancelOrder(OrderId orderId) {
    std::scoped_lock ordersLock( ordersMutex_ );

    CancelOrderInternal(orderId);
}

// Modifying the order is just a cancel order and add order call
Trades Orderbook::MatchOrder(OrderModify order) {
    // If the order doesn't exist return empty
    if (!orders_.contains(order.GetOrderId()))
        return { };

    // Get the existingOrder pointer from "fast access table"
    const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
    const auto orderType = existingOrder->GetOrderType();
    // Cancel the order
    CancelOrder(order.GetOrderId());
    // Add a new order with order pointer to new order
    return AddOrder(order.ToOrderPointer(orderType));
}

// Function Size() that returns the size of the orders_ table
std::size_t Orderbook::Size() const { return orders_.size(); }

OrderbookLevelInfos Orderbook::GetOrderInfo() const {
    LevelInfos bidInfos, askInfos;
    bidInfos.reserve(bids_.size());
    askInfos.reserve(asks_.size());

    auto CreateLevelInfos = [](Price price, const OrderPointers& orders) {
        return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
            [](Quantity runningSum, const OrderPointer& order)
            {return runningSum + order->GetRemainingQuantity(); }) };
    };

    for (const auto& [price, orders] : bids_)
        bidInfos.push_back(CreateLevelInfos(price, orders));

    for (const auto& [price, orders] : asks_)
        askInfos.push_back(CreateLevelInfos(price, orders));

    return OrderbookLevelInfos{ bidInfos, askInfos };
}