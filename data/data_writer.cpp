#include "data_writer.hpp"

DataWriter::DataWriter(const std::string& filename)
    : file_(filename) {

    write_header();
}

DataWriter::~DataWriter() {
    if (file_.is_open()) {
        file_.close();
    }
}

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
        << "spread\n";
}