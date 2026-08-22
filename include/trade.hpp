//makes sure this file is only read once
#pragma once

//gives access to integer types with specified sizes
#include <cstdint>

//includes the order.hpp file to use the Order structure and related types
#include "order.hpp"

//trade record structure to represent a trade in the trading system
struct Trade {
    OrderId incoming_order_id;
    OrderId resting_order_id;
    TraderId buyer_id;
    TraderId seller_id;
    Price price;
    Quantity quantity;
    Timestamp timestamp;
};
