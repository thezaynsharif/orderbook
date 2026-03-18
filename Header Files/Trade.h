#pragma once

#include "TradeInfo.h"

#ifndef ORDERBOOK_TRADE_H
#define ORDERBOOK_TRADE_H

#endif //ORDERBOOK_TRADE_H

class Trade {
public:
    Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
        : bidTrade_{ bidTrade }
    , askTrade_{ askTrade }
    {}

    const TradeInfo& GetBidTrade() const { return bidTrade_; }
    const TradeInfo& GetAskTrade() const { return askTrade_; }

private:
    TradeInfo bidTrade_;
    TradeInfo askTrade_;
};

// One order could sweep up a bunch of different orders so we make a list of Trades
using Trades = std::vector<Trade>;