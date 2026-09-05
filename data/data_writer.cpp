#include "data_writer.hpp"

//how many price levels on each side the depth columns are totalled over
constexpr std::size_t depth_levels = 5;

DataWriter::DataWriter(const std::string& filename)
    : file_(filename) {

    write_header();
}

DataWriter::~DataWriter() {
    if (file_.is_open()) {
        file_.close();
    }
}

//writes the header row to the output file
void DataWriter::write_header() {

    file_
        << "event,"
        << "event_type,"
        << "order_id,"
        << "side,"
        << "price,"
        << "quantity,"
        << "best_bid,"
        << "best_ask,"
        << "spread,"
        << "mid_price,"
        << "bid_depth,"
        << "ask_depth\n";
}

//writes a order event to the output file
void DataWriter::write_event(
    std::size_t event_number,
    const Order& order,
    const OrderBook& order_book
) {

    file_ << event_number << ",";
    file_ << "NEW_ORDER" << ",";
    file_ << order.id << ",";

    if (order.side == Side::Buy) {
        file_ << "BUY";
    } else {
        file_ << "SELL";
    }

    file_ << ",";
    file_ << order.price << ",";
    file_ << order.remaining_quantity << ",";

    auto best_bid = order_book.best_bid();
    auto best_ask = order_book.best_ask();
    auto spread = order_book.spread();
    auto mid_price = order_book.mid_price();

    if (best_bid.has_value()) {
        file_ << best_bid.value();
    }

    file_ << ",";

    if (best_ask.has_value()) {
        file_ << best_ask.value();
    }

    file_ << ",";

    if (spread.has_value()) {
        file_ << spread.value();
    }

    file_ << ",";

    if (mid_price.has_value()) {
        file_ << mid_price.value();
    }

    file_ << ",";

    //total quantity resting in the price levels nearest the top of each side of the book
    Quantity bid_quantity = 0;
    for (const auto& level : order_book.bid_depth(depth_levels)) {
        bid_quantity += level.quantity;
    }

    Quantity ask_quantity = 0;
    for (const auto& level : order_book.ask_depth(depth_levels)) {
        ask_quantity += level.quantity;
    }

    file_ << bid_quantity << ",";
    file_ << ask_quantity;

    file_ << "\n";
}

//writes a trade event to the output file
void DataWriter::write_trade(
    std::size_t event_number,
    const Trade& trade
) {

    file_ << event_number << ",";
    file_ << "TRADE" << ",";

    file_ << trade.incoming_order_id << ",";
    //a trade has both a buyer and a seller, so the side column is left empty
    file_ << ",";
    file_ << trade.price << ",";
    file_ << trade.quantity << ",";
    file_ << ",,,,,\n";
}