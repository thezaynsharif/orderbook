#pragma once

#include "LevelInfo.h"

#ifndef ORDERBOOK_ORDERBOOKLEVELINFOS_H
#define ORDERBOOK_ORDERBOOKLEVELINFOS_H

#endif //ORDERBOOK_ORDERBOOKLEVELINFOS_H

// Orderbook class of list of Level Informations
class OrderbookLevelInfos {
public:
    // Constructor (is called when new object of class OrderbookLevelInfos is created
    // bids_ is initialised with bids
    // asks_ is initialised with asks
    OrderbookLevelInfos(const LevelInfos& bids, const LevelInfos& asks)
    : bids_{ bids }, asks_{ asks }
    {}

    // Getter allow for read only of the private variables: bids_, asks_
    const LevelInfos& GetBids() const { return bids_; }
    const LevelInfos& GetAsks() const { return asks_; }

private:
    LevelInfos bids_;
    LevelInfos asks_;
};