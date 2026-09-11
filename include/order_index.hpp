//makes sure this file is only read once
#pragma once

#include "order.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

//a hash table from order id to where the order is held, stored in one flat array
template <typename Value>
class OrderIndex {
    public:
        OrderIndex() {
            slots_.resize(initial_capacity);
        }

        //adds an order, or updates it if the id is already there
        void insert(OrderId id, const Value& value) {
            if ((occupied_ + 1) * 10 >= slots_.size() * maximum_load_percent) {
                grow();
            }

            std::size_t index = bucket(id);

            while (slots_[index].occupied) {
                if (slots_[index].id == id) {
                    slots_[index].value = value;
                    return;
                }

                index = next(index);
            }

            slots_[index].occupied = true;
            slots_[index].id = id;
            slots_[index].value = value;
            occupied_++;
        }

        //returns where the order is held, or nullptr if it is not in the table
        const Value* find(OrderId id) const {
            std::size_t index = bucket(id);

            while (slots_[index].occupied) {
                if (slots_[index].id == id) {
                    return &slots_[index].value;
                }

                index = next(index);
            }

            return nullptr;
        }

        //removes an order and returns whether it was there
        bool erase(OrderId id) {
            std::size_t index = bucket(id);

            while (slots_[index].occupied && slots_[index].id != id) {
                index = next(index);
            }

            if (!slots_[index].occupied) {
                return false;
            }

            slots_[index].occupied = false;
            occupied_--;

            //pull later entries back into the gap, unless that would put one before its bucket
            std::size_t gap = index;
            std::size_t probe = index;

            for (;;) {
                probe = next(probe);

                if (!slots_[probe].occupied) {
                    break;
                }

                std::size_t ideal = bucket(slots_[probe].id);

                bool leave_where_it_is;
                if (gap <= probe) {
                    leave_where_it_is = (gap < ideal) && (ideal <= probe);
                } else {
                    leave_where_it_is = (gap < ideal) || (ideal <= probe);
                }

                if (leave_where_it_is) {
                    continue;
                }

                slots_[gap] = slots_[probe];
                slots_[probe].occupied = false;
                gap = probe;
            }

            return true;
        }

        std::size_t size() const {
            return occupied_;
        }

        std::size_t capacity() const {
            return slots_.size();
        }

        //makes room for this many entries up front, keeping anything already in the table
        void reserve(std::size_t entries) {
            std::size_t capacity = slots_.size();

            while (entries * 10 >= capacity * maximum_load_percent) {
                capacity *= 2;
            }

            if (capacity > slots_.size()) {
                rebuild(capacity);
            }
        }

    private:
        struct Slot {
            OrderId id = 0;
            Value value{};
            bool occupied = false;
        };

        //starting number of slots
        static constexpr std::size_t initial_capacity = 64;

        //the table grows once it is seven tenths full
        static constexpr std::size_t maximum_load_percent = 7;

        std::vector<Slot> slots_;
        std::size_t occupied_ = 0;

        //fibonacci hashing, so ids that arrive in sequence spread across the table
        std::size_t bucket(OrderId id) const {
            std::uint64_t mixed = static_cast<std::uint64_t>(id) * 0x9E3779B97F4A7C15ull;
            return static_cast<std::size_t>(mixed >> shift_);
        }

        //the next slot along, wrapping round at the end of the table
        std::size_t next(std::size_t index) const {
            return (index + 1) & (slots_.size() - 1);
        }

        void grow() {
            rebuild(slots_.size() * 2);
        }

        //moves every entry into a new table of the given size, a power of two
        void rebuild(std::size_t capacity) {
            std::vector<Slot> old_slots = std::move(slots_);

            slots_.clear();
            slots_.resize(capacity);

            //works out the shift from the new table size
            std::size_t bits = 0;
            while ((static_cast<std::size_t>(1) << bits) < capacity) {
                bits++;
            }
            shift_ = 64 - bits;

            occupied_ = 0;

            for (const Slot& slot : old_slots) {
                if (slot.occupied) {
                    insert(slot.id, slot.value);
                }
            }
        }

        //how far to shift the hash to land inside the table, 64 minus log2 of the size
        std::size_t shift_ = 58;
};
