//makes sure this file is only read once
#pragma once

//gives access to integer types with specified sizes
#include <cstddef>
#include <cstdint>

//includes the order.hpp file to use the Order structure and related types
#include "order.hpp"

//map and functional
#include <map>
#include <functional>

//a slot index that points at nothing, like a null pointer
constexpr std::size_t no_order = static_cast<std::size_t>(-1);

//defines a PriceLevel as the head and tail of its queue of orders in the order book
struct PriceLevel {
    std::size_t head = no_order;
    std::size_t tail = no_order;

    //total quantity resting at this price, kept up to date as orders come and go
    Quantity total_quantity = 0;
};

using AskBook = std::map<Price, PriceLevel>;
using BidBook = std::map<Price, PriceLevel, std::greater<Price>>;
