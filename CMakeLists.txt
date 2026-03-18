cmake_minimum_required(VERSION 4.1)
project(orderbook)

set(CMAKE_CXX_STANDARD 20)

add_executable(orderbook "Source Files/main.cpp"
        "Source Files/Orderbook.cpp"
        "Header Files/Orderbook.h"
        "Header Files/Usings.h"
        "Header Files/Order.h"
        "Header Files/OrderModify.h"
        "Header Files/OrderbookLevelInfos.h"
        "Header Files/LevelInfo.h"
        "Header Files/Trade.h"
        "Header Files/TradeInfo.h"
        "Header Files/OrderType.h"
        "Header Files/Side.h"
        "Header Files/Constants.h")

add_subdirectory(Google_tests)