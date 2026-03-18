#pragma once

#include <limits>

#include "Usings.h"

#ifndef ORDERBOOK_CONSTANTS_H
#define ORDERBOOK_CONSTANTS_H

#endif //ORDERBOOK_CONSTANTS_H

struct Constants
{
    // Invalid Price is just NaN, it is Not a Number. For Orders where this is used, we do not care about the price
    static const Price InvalidPrice = std::numeric_limits<Price>::quiet_NaN();
};