#include <gtest/gtest.h>
#include "Orderbook.h"

// ─────────────────────────────────────────────────────────────────────────────
// Factory helpers — keep test bodies terse
// ─────────────────────────────────────────────────────────────────────────────

static OrderPointer MakeOrder(OrderType type, OrderId id, Side side,
                               Price price, Quantity qty)
{
    return std::make_shared<Order>(type, id, side, price, qty);
}

// Market-order constructor omits price (uses the two-arg Order ctor)
static OrderPointer MakeMarketOrder(OrderId id, Side side, Quantity qty)
{
    return std::make_shared<Order>(OrderType::Market, id, side, qty);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixture — each test gets a fresh, empty Orderbook
// ─────────────────────────────────────────────────────────────────────────────

class OrderbookTest : public ::testing::Test
{
protected:
    Orderbook book;
};

// =============================================================================
// 1. Basic Add & Remove
// =============================================================================

// Verify that a GoodTillCancel limit order is placed and tracked by the book
TEST_F(OrderbookTest, AddLimitOrder_AppearsInBook)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    EXPECT_EQ(book.Size(), 1u);
}

// Verify that cancelling an existing order removes it from the book entirely
TEST_F(OrderbookTest, CancelOrder_ExistingOrder_RemovedFromBook)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    book.CancelOrder(1);
    EXPECT_EQ(book.Size(), 0u);
}

// Verify that bid and ask level infos are populated after adding non-matching orders
TEST_F(OrderbookTest, GetOrderInfo_ReflectsAddedOrders)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy,  99, 5));
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Sell, 101, 7));

    const auto info = book.GetOrderInfo();
    ASSERT_EQ(info.GetBids().size(), 1u);
    ASSERT_EQ(info.GetAsks().size(), 1u);
    EXPECT_EQ(info.GetBids()[0].price_,    99);
    EXPECT_EQ(info.GetBids()[0].quantity_,  5u);
    EXPECT_EQ(info.GetAsks()[0].price_,    101);
    EXPECT_EQ(info.GetAsks()[0].quantity_,  7u);
}

// Duplicate order IDs must be silently rejected — the book should not change size
TEST_F(OrderbookTest, AddDuplicateOrderId_Rejected)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    auto trades = book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 5));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 1u);
}

// =============================================================================
// 2. Order Matching — full fill
// =============================================================================

// Two opposing GTC orders at the same price should fully fill each other,
// generate exactly one Trade, and leave the book empty
TEST_F(OrderbookTest, MatchOrders_TwoOpposingLimits_FullFill)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy,  100, 10));
    auto trades = book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Sell, 100, 10));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(book.Size(), 0u);
    // Trade should reference the correct order IDs
    EXPECT_EQ(trades[0].GetBidTrade().orderId_,  1u);
    EXPECT_EQ(trades[0].GetAskTrade().orderId_,  2u);
    EXPECT_EQ(trades[0].GetBidTrade().quantity_, 10u);
    EXPECT_EQ(trades[0].GetAskTrade().quantity_, 10u);
}

// =============================================================================
// 3. Partial Fill
// =============================================================================

// A large resting bid matched against a smaller ask should leave the bid's
// residual quantity in the book; the ask should be fully removed
TEST_F(OrderbookTest, MatchOrders_PartialFill_ResidualRemainsInBook)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy,  100, 10));
    auto trades = book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Sell, 100,  3));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetBidTrade().quantity_, 3u);

    // Bid (order 1) is still resting with 7 remaining; ask (order 2) is gone
    EXPECT_EQ(book.Size(), 1u);

    const auto info = book.GetOrderInfo();
    ASSERT_EQ(info.GetBids().size(), 1u);
    EXPECT_EQ(info.GetBids()[0].quantity_, 7u);   // 10 - 3 = 7 remaining
    EXPECT_TRUE(info.GetAsks().empty());
}

// =============================================================================
// 4. Price Priority
// =============================================================================

// When multiple bids sit at different prices, the highest bid must match first
// (best price wins before time priority)
TEST_F(OrderbookTest, PricePriority_HighestBidMatchesFirst)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 5));
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Buy, 101, 5)); // better price

    // Incoming sell at 100 — should fill bid at 101 first
    auto trades = book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 3, Side::Sell, 100, 5));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetBidTrade().orderId_, 2u); // order 2 (101) matched, not order 1 (100)
    EXPECT_EQ(book.Size(), 1u);                      // order 1 (100) still resting
}

// =============================================================================
// 5. Time Priority (FIFO)
// =============================================================================

// Among bids at the same price, the earliest-submitted order must fill first
TEST_F(OrderbookTest, TimePriority_FIFOAtSamePrice)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 5)); // arrives first
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Buy, 100, 5)); // arrives second

    // Incoming sell fills exactly 5 — should consume order 1 in FIFO order
    auto trades = book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 3, Side::Sell, 100, 5));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetBidTrade().orderId_, 1u); // order 1 filled first
    EXPECT_EQ(book.Size(), 1u);                      // order 2 still resting
}

// =============================================================================
// 6. GoodTillCancel
// =============================================================================

// A GTC order must persist in the book across non-matching activity and only
// disappear once explicitly cancelled
TEST_F(OrderbookTest, GoodTillCancel_PersistsUntilExplicitlyCancelled)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy,  100, 10));
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Sell, 105, 10)); // no match

    EXPECT_EQ(book.Size(), 2u); // both orders are still resting

    book.CancelOrder(1);
    EXPECT_EQ(book.Size(), 1u);

    book.CancelOrder(2);
    EXPECT_EQ(book.Size(), 0u);
}

// =============================================================================
// 7. FillAndKill
// =============================================================================

// A FAK order that partially fills must have its unfilled remainder killed;
// it must never rest in the book
TEST_F(OrderbookTest, FillAndKill_PartialFill_RemainderIsKilled)
{
    // Resting sell of 5 — the FAK buy wants 10 but can only get 5
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    auto trades = book.AddOrder(MakeOrder(OrderType::FillAndKill,    2, Side::Buy,  100, 10));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetBidTrade().quantity_, 5u); // filled the available 5
    EXPECT_EQ(book.Size(), 0u);                       // FAK remainder was killed; sell fully consumed
}

// A FAK order that finds no opposing liquidity at all must be rejected without
// entering the book
TEST_F(OrderbookTest, FillAndKill_NoLiquidity_OrderRejectedEntirely)
{
    auto trades = book.AddOrder(MakeOrder(OrderType::FillAndKill, 1, Side::Buy, 100, 10));

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 0u);
}

// =============================================================================
// 8. FillOrKill
// =============================================================================

// A FOK order where the available quantity is insufficient must be rejected
// in full — the resting order on the other side must be untouched
TEST_F(OrderbookTest, FillOrKill_InsufficientLiquidity_EntireOrderRejected)
{
    // Only 3 units available; FOK wants 10
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Sell, 100, 3));
    auto trades = book.AddOrder(MakeOrder(OrderType::FillOrKill,    2, Side::Buy,  100, 10));

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 1u); // resting sell is unchanged — no partial fill occurred
}

// A FOK order that can be fully satisfied must execute normally and clear both sides
TEST_F(OrderbookTest, FillOrKill_SufficientLiquidity_ExecutesFullTrade)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));
    auto trades = book.AddOrder(MakeOrder(OrderType::FillOrKill,    2, Side::Buy,  100, 10));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetBidTrade().quantity_, 10u);
    EXPECT_EQ(book.Size(), 0u);
}

// A FOK order satisfied by liquidity spread across multiple price levels
// must execute completely (CanFullyFill must sum across levels)
TEST_F(OrderbookTest, FillOrKill_LiquiditySpreadAcrossLevels_ExecutesFullTrade)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Sell, 101, 5));
    // FOK buy at 101 for 10 — 5 available at 100, 5 at 101 — total = 10
    auto trades = book.AddOrder(MakeOrder(OrderType::FillOrKill,    3, Side::Buy,  101, 10));

    ASSERT_EQ(trades.size(), 2u); // one trade per resting order swept
    EXPECT_EQ(book.Size(), 0u);
}

// =============================================================================
// 9. GoodForDay
// =============================================================================

// A GFD order must behave identically to GTC for matching purposes during a session
TEST_F(OrderbookTest, GoodForDay_MatchesNormallyDuringSession)
{
    book.AddOrder(MakeOrder(OrderType::GoodForDay, 1, Side::Buy, 100, 10));
    EXPECT_EQ(book.Size(), 1u);

    auto trades = book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Sell, 100, 10));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(book.Size(), 0u);
}

// A GFD order that does not match must rest in the book (it is not immediately killed)
TEST_F(OrderbookTest, GoodForDay_NonMatchingOrder_RestsInBook)
{
    book.AddOrder(MakeOrder(OrderType::GoodForDay, 1, Side::Buy, 99, 10));
    book.AddOrder(MakeOrder(OrderType::GoodForDay, 2, Side::Sell, 101, 10));
    EXPECT_EQ(book.Size(), 2u);
}

// =============================================================================
// 10. Market Order
// =============================================================================

// A market buy must match against the best available ask immediately,
// using the worst-ask price conversion so it sweeps available liquidity
TEST_F(OrderbookTest, MarketOrder_Buy_MatchesBestAsk)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));
    auto trades = book.AddOrder(MakeMarketOrder(2, Side::Buy, 10));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetBidTrade().quantity_, 10u);
    EXPECT_EQ(book.Size(), 0u);
}

// A market sell must match against the best available bid immediately
TEST_F(OrderbookTest, MarketOrder_Sell_MatchesBestBid)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    auto trades = book.AddOrder(MakeMarketOrder(2, Side::Sell, 10));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetAskTrade().quantity_, 10u);
    EXPECT_EQ(book.Size(), 0u);
}

// A market order submitted to an empty opposite side must be rejected silently
// without entering the book
TEST_F(OrderbookTest, MarketOrder_NoOppositeOrders_Rejected)
{
    auto trades = book.AddOrder(MakeMarketOrder(1, Side::Buy, 10));

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 0u);
}

// =============================================================================
// 11. No-match scenario
// =============================================================================

// When the best bid is below the best ask, no trade should fire and both
// orders should rest in the book indefinitely
TEST_F(OrderbookTest, NoMatch_BidBelowAsk_BothOrdersRest)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy,   99, 10));
    auto trades = book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Sell, 101, 10));

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 2u);

    const auto info = book.GetOrderInfo();
    EXPECT_EQ(info.GetBids().size(), 1u);
    EXPECT_EQ(info.GetAsks().size(), 1u);
}

// =============================================================================
// 12. Empty book / non-existent order
// =============================================================================

// Cancelling an order ID that does not exist must not crash and must leave
// the book in a valid (still empty) state
TEST_F(OrderbookTest, CancelNonExistentOrder_HandledGracefully)
{
    EXPECT_NO_THROW(book.CancelOrder(9999));
    EXPECT_EQ(book.Size(), 0u);
}

// Cancelling a non-existent ID while other orders are live must not disturb
// unrelated resting orders
TEST_F(OrderbookTest, CancelNonExistentOrder_DoesNotAffectExistingOrders)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 5));
    EXPECT_NO_THROW(book.CancelOrder(9999));
    EXPECT_EQ(book.Size(), 1u);
}

// =============================================================================
// 13. Multiple fills — one aggressive order sweeps several price levels
// =============================================================================

// One large buy order priced high enough to cross three ask levels must
// generate a separate Trade for each resting order it consumes, and the
// trades must be filled in ascending ask-price order (price priority)
TEST_F(OrderbookTest, MultipleFills_AggressiveBuySweepsThreeLevels)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Sell, 101, 5));
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 3, Side::Sell, 102, 5));

    // Buy 15 at 102 — sweeps all three ask levels
    auto trades = book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 4, Side::Buy, 102, 15));

    ASSERT_EQ(trades.size(), 3u);
    EXPECT_EQ(book.Size(), 0u);

    // Trades must be ordered cheapest ask first (price priority)
    EXPECT_EQ(trades[0].GetAskTrade().orderId_,   1u);
    EXPECT_EQ(trades[0].GetAskTrade().price_,    100);
    EXPECT_EQ(trades[0].GetAskTrade().quantity_,   5u);

    EXPECT_EQ(trades[1].GetAskTrade().orderId_,   2u);
    EXPECT_EQ(trades[1].GetAskTrade().price_,    101);

    EXPECT_EQ(trades[2].GetAskTrade().orderId_,   3u);
    EXPECT_EQ(trades[2].GetAskTrade().price_,    102);
}

// A large aggressive order that only partially sweeps available levels must
// leave the unfilled resting orders untouched
TEST_F(OrderbookTest, MultipleFills_PartialSweep_ResidualsRemainInBook)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Sell, 101, 5));
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 3, Side::Sell, 102, 5));

    // Buy 8 at 101 — fills all of level 100 (5) and 3 from level 101, stops there
    auto trades = book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 4, Side::Buy, 101, 8));

    ASSERT_EQ(trades.size(), 2u);
    // Level 100 fully consumed; level 101 partially consumed (2 remain); level 102 untouched
    EXPECT_EQ(book.Size(), 2u); // partial remnant at 101 + untouched order at 102

    const auto info = book.GetOrderInfo();
    ASSERT_EQ(info.GetAsks().size(), 2u);
    // asks_ is sorted lowest first; index 0 = 101, index 1 = 102
    EXPECT_EQ(info.GetAsks()[0].price_,    101);
    EXPECT_EQ(info.GetAsks()[0].quantity_,   2u); // 5 - 3 = 2 remaining
    EXPECT_EQ(info.GetAsks()[1].price_,    102);
    EXPECT_EQ(info.GetAsks()[1].quantity_,   5u); // untouched
}

// =============================================================================
// 14. MatchOrder (modify)
// =============================================================================

// Modifying an order's price should cancel the original and re-add with new price
TEST_F(OrderbookTest, MatchOrder_ModifyPrice_UpdatesBook)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 99, 10));
    EXPECT_EQ(book.Size(), 1u);

    // Modify: same ID, new price 100, same side and quantity
    book.MatchOrder(OrderModify{ 1, Side::Buy, 100, 10 });
    EXPECT_EQ(book.Size(), 1u);

    const auto info = book.GetOrderInfo();
    ASSERT_EQ(info.GetBids().size(), 1u);
    EXPECT_EQ(info.GetBids()[0].price_, 100); // price updated
}

// Modifying an order to a price that crosses the spread should trigger a match
TEST_F(OrderbookTest, MatchOrder_NewPriceCrossesSpread_TriggersTrade)
{
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy,   99, 10));
    book.AddOrder(MakeOrder(OrderType::GoodTillCancel, 2, Side::Sell, 100, 10));
    EXPECT_EQ(book.Size(), 2u);

    // Raise the bid to 100 — should now match the resting ask
    auto trades = book.MatchOrder(OrderModify{ 1, Side::Buy, 100, 10 });

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(book.Size(), 0u);
}
