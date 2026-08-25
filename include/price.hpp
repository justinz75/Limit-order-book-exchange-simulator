//makes sure this file is only read once
#pragma once

//gives access to integer types with specified sizes
#include <cstdint>

//includes the order.hpp file to use the Order structure and related types
#include "order.hpp"

//list, map, and functional
#include <list>
#include <map>
#include <functional>

 //defines PriceLevel as a list of Order objects
using PriceLevel = std::list<Order>;
using AskBook = std::map<Price, PriceLevel>;
using BidBook = std::map<Price, PriceLevel, std::greater<Price>>;