//makes sure this file is only read once
#pragma once

//gives access to integer types with specified sizes
#include <cstdint>

//creates enumerations for the side of the order (buy or sell)
enum class Side {
    Buy,
    Sell
};

//creates an enumeration for the type of order (limit or market)
enum class OrderType {
    Limit,
    Market
};

//'std::uint64_t' is an unsigned integer type that can hold values from 0 to 2^64 - 1, which is suitable for representing quantities in this context.
//'std::int64_t' is a signed integer type that can hold values from -2^63 to 2^63 - 1, which is suitable for representing prices in this context.
using OrderId = std::uint64_t;
using TraderId = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::uint64_t;
using Timestamp = std::uint64_t;

//defines a structure to represent an order in the trading system
struct Order {
    OrderId id;
    TraderId trader_id;
    Side side;
    OrderType type;
    Price price;
    Quantity remaining_quantity;
    Timestamp timestamp;
};
