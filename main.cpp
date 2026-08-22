//add order.hpp and trade.hpp
#include "order.hpp"
#include "trade.hpp"
#include "price.hpp"
#include <iostream>

//includes the list header to use the std::list container
#include <list>
using namespace std;

int main() {
    //creates a PriceLevel object to hold orders at a specific price level
    PriceLevel price_level;
    //creates an empty sell book
    AskBook asks;
    BidBook bids;
    //example usage of the Order structure
    Order order;
    order.id = 1;
    order.trader_id = 1;
    order.side = Side::Buy;
    order.type = OrderType::Limit;
    order.price = 100; //price in cents
    order.remaining_quantity = 10; //quantity of shares
    order.timestamp = 1; //example timestamp
    cout << "Order ID: " << order.id << endl;
    return 0;
}