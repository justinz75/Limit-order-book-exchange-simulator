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

//stands in for a slot index that does not point at anything, the way a null pointer would
constexpr std::size_t no_order = static_cast<std::size_t>(-1);

//the orders resting at one price. they are not held here: they live in a single arena inside the order
//book and are linked to each other by slot index, so a level only needs to know where its queue starts
//and ends. head is the order that arrived first, which is the one that trades first
struct PriceLevel {
    std::size_t head = no_order;
    std::size_t tail = no_order;

    //kept up to date as orders join, leave and are partly filled, so the depth queries do not have to
    //walk the queue to add it up
    Quantity total_quantity = 0;
};

using AskBook = std::map<Price, PriceLevel>;
using BidBook = std::map<Price, PriceLevel, std::greater<Price>>;
