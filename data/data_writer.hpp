#pragma once

#include "order_book.hpp"

#include <cstddef>
#include <fstream>
#include <string>

class DataWriter {
public:
    explicit DataWriter(const std::string& filename);

    ~DataWriter();

    void write_header();

private:
    std::ofstream file_;
};