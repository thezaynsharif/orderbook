# C++ Limit Order Book Engine

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![CMake](https://img.shields.io/badge/CMake-4.1%2B-064F8C.svg)
![Google Test](https://img.shields.io/badge/tests-24%20passing-brightgreen.svg)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)

A central limit order book (CLOB) engine written in C++20. Implements the core matching logic used in exchanges and electronic trading venues — price-time priority matching, five order types, and a thread-safe architecture with a background session-expiry thread.

---

## Features

### Order Types

| Type | Behaviour |
|---|---|
| `GoodTillCancel` | Rests in the book indefinitely until explicitly cancelled or filled |
| `FillAndKill` | Fills as much as immediately available; any unfilled remainder is cancelled |
| `FillOrKill` | Must fill entirely in one shot — rejected in full if insufficient liquidity exists at submission time |
| `GoodForDay` | Rests like GTC during the trading session; automatically cancelled at 4 PM by a background pruning thread |
| `Market` | Converted internally to a GTC at the worst available opposite price, ensuring it sweeps through all available price levels aggressively |

### Operations

| Method | Description |
|---|---|
| `AddOrder(OrderPointer)` | Submit a new order; returns any `Trades` generated |
| `CancelOrder(OrderId)` | Remove a resting order by ID; no-op if ID is unknown |
| `MatchOrder(OrderModify)` | Modify price/quantity of an existing order (cancel + re-add); returns any `Trades` generated |
| `Size()` | Number of live orders currently resting in the book |
| `GetOrderInfo()` | Snapshot of all price levels with aggregate quantities for both sides |

---

## Architecture

### Data Structures

The design prioritises O(1) cancellation and O(log n) insertion — the two most common operations in a high-throughput order book.

| Field | Type | Purpose |
|---|---|---|
| `bids_` | `std::map<Price, OrderPointers, std::greater<Price>>` | Descending sort — `begin()` is always the best bid (highest price) |
| `asks_` | `std::map<Price, OrderPointers, std::less<Price>>` | Ascending sort — `begin()` is always the best ask (lowest price) |
| `orders_` | `std::unordered_map<OrderId, OrderEntry>` | O(1) order lookup and cancellation by ID |
| Per-level queue | `std::list<OrderPointer>` | Each order stores an iterator to its own node, enabling O(1) removal while preserving FIFO time priority |
| `data_` | `std::unordered_map<Price, LevelData>` | O(1) per-level quantity/count cache used by `FillOrKill` pre-checks |

### Matching Logic

Matching runs on every `AddOrder` call:

1. Retrieve `bids_.begin()` (best bid) and `asks_.begin()` (best ask)
2. If `bidPrice < askPrice`, no trade is possible — exit
3. Otherwise, fill front-of-queue orders at both levels, popping fully-filled orders and erasing empty price levels
4. Repeat until the spread is uncrossed or a side is exhausted

This gives **price-time priority**: best price matches first; among equal prices, the earliest-submitted order fills first (FIFO queue per level).

### FillOrKill Pre-Check (`CanFullyFill`)

Before accepting a FOK order, the engine sums available quantity across all price levels that would be crossed. If the total is insufficient, the order is rejected immediately without touching the book — ensuring no partial fills ever occur.

### Thread Safety

`GoodForDay` expiry is handled by a dedicated background thread (`ordersPruneThread_`) that sleeps via a `std::condition_variable` until 4 PM, then cancels all resting GFD orders under a `std::mutex`. The same mutex protects all public mutations (`AddOrder`, `CancelOrder`, `MatchOrder`). The thread wakes immediately on destructor invocation via `shutdown_` flag + `notify_one()`.

---

## Type Aliases

```cpp
using Price    = std::int32_t;   // fixed-point integer price
using Quantity = std::uint32_t;  // unsigned share/contract quantity
using OrderId  = std::uint64_t;  // unique order identifier
using Trades   = std::vector<Trade>;
```

---

## Public API

```cpp
class Orderbook {
public:
    Trades              AddOrder(OrderPointer order);
    void                CancelOrder(OrderId orderId);
    Trades              MatchOrder(OrderModify order);  // modify = cancel + re-add
    std::size_t         Size() const;
    OrderbookLevelInfos GetOrderInfo() const;
};
```

Each `Trade` carries two `TradeInfo` records — one for the bid side and one for the ask side — each containing `orderId_`, `price_`, and `quantity_`.

---

## Build

**Requirements:** CMake 3.14+, a C++20-capable compiler (GCC 11+, Clang 13+, MSVC 19.29+)

```bash
git clone https://github.com/thezaynsharif/orderbook.git
cd orderbook
cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
./cmake-build-debug/orderbook
```

**CLion:** Open the root `CMakeLists.txt` directly — CLion will configure CMake automatically.

---

## Tests

The test suite uses [Google Test](https://github.com/google/googletest), fetched automatically at configure time via `FetchContent`.

```bash
cd cmake-build-debug
ctest --output-on-failure
```

Or select the `Google_Tests_run` target in CLion and press Run.

**24 tests across 14 groups:**

| Group | What is tested |
|---|---|
| Basic Add & Remove | Order placement, cancellation, `GetOrderInfo`, duplicate ID rejection |
| Order Matching | Full fill between two opposing limit orders |
| Partial Fill | Residual quantity remains after a smaller opposing order executes |
| Price Priority | Highest bid (or lowest ask) matches before worse-priced orders |
| Time Priority | FIFO ordering among orders at the same price level |
| GoodTillCancel | Persists until explicitly cancelled |
| FillAndKill | Partial fill + remainder killed; no-liquidity rejection |
| FillOrKill | Full rejection when insufficient liquidity; full execution when sufficient |
| GoodForDay | Matches normally during session; rests when no match |
| Market Order | Fills against best bid/ask; rejected on empty opposite side |
| No-Match Scenario | Bid below ask — both orders rest, no trade fires |
| Empty Book | Cancel of unknown ID is a safe no-op |
| Multiple Fills | Aggressive order sweeps multiple price levels; partial sweep leaves residuals |
| MatchOrder (Modify) | Price update via cancel + re-add; modify that crosses spread triggers trade |

---

## Known Limitations

- **`Constants::InvalidPrice`** — `std::numeric_limits<int32_t>::quiet_NaN()` returns `0` on integer types, making `InvalidPrice == 0`. Market orders with no opposing liquidity are handled defensively, but the constant itself is semantically misleading.
- **`CanFullyFill` operator precedence** — The threshold-skip condition uses `&&` and `||` without explicit parentheses, which can cause the check to skip levels incorrectly in certain edge cases.
- **`FillAndKill` cleanup scope** — `MatchOrders` only checks the front of the best price level for FAK cleanup. A FAK order sitting deeper within a level (after partial fills by other orders) would not be caught.
- **GoodForDay prune time** — The 4 PM cutoff is hardcoded; there is no timezone or configurable cutoff support.

---

## Project Structure

```
orderbook/
├── Header Files/
│   ├── Orderbook.h           # Main orderbook class
│   ├── Order.h               # Order class + Fill/ToGoodTillCancel logic
│   ├── OrderModify.h         # Modify request (cancel + re-add)
│   ├── OrderType.h           # Enum: GoodTillCancel, FillAndKill, FillOrKill, GoodForDay, Market
│   ├── Side.h                # Enum: Buy, Sell
│   ├── Trade.h / TradeInfo.h # Trade result types
│   ├── LevelInfo.h           # Price level snapshot
│   ├── OrderbookLevelInfos.h # Bid/ask level snapshot container
│   ├── Usings.h              # Type aliases (Price, Quantity, OrderId)
│   └── Constants.h           # InvalidPrice sentinel
├── Source Files/
│   ├── Orderbook.cpp         # Matching engine implementation
│   └── main.cpp              # Minimal usage example
├── Google_tests/
│   ├── test.cpp              # 24-test suite
│   └── CMakeLists.txt        # FetchContent gtest + test target
└── CMakeLists.txt            # Root build config
```
