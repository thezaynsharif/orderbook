#pragma once

#include "Usings.h"

#ifndef ORDERBOOK_LEVELINFO_H
#define ORDERBOOK_LEVELINFO_H

#endif //ORDERBOOK_LEVELINFO_H

// Like record structure in Java. Defining orderbook level information which is made up of Price and Quantity
struct LevelInfo {
    Price price_;
    Quantity quantity_;
};

// Type alias of std::vector<LevelInfo> which is like an array of records of record LevelInfo
using LevelInfos = std::vector<LevelInfo>;