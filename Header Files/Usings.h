#pragma once

#include <vector>

#ifndef ORDERBOOK_USINGS_H
#define ORDERBOOK_USINGS_H

#endif //ORDERBOOK_USINGS_H


// Type aliases "Price is another name for std::int32_t" etc. benefits readability
using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;
using OrderIds = std::vector<OrderId>;