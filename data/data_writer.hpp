#pragma once

#include "order_book.hpp"

#include <cstddef>
#include <fstream>
#include <string>

class DataWriter {
public:
    //an empty filename gives a writer that records nothing at all, which is what the experiments use
    //for the extra seeds they only want the totals from
    explicit DataWriter(const std::string& filename);

    ~DataWriter();

    void write_header();
    
    void write_event(
        std::size_t event_number,
        const Order& order,
        const OrderBook& order_book
    );

    void write_trade(
        std::size_t event_number,
        const Trade& trade
    );
private:
    std::ofstream file_;
    bool enabled_ = false;
};

enum class EventType {
    NewOrder,
    Cancel,
    Trade
};