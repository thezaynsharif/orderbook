#pragma once

#ifndef ORDERBOOK_ORDERTYPE_H
#define ORDERBOOK_ORDERTYPE_H

#endif //ORDERBOOK_ORDERTYPE_H


enum class OrderType {
    GoodTillCancel,
    FillAndKill,
    FillOrKill,
    GoodForDay,
    Market,
};