//makes sure this file is only read once
#pragma once

#include "order.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

//a hash table from order id to wherever that order is being held, laid out in one flat array.
//
//this was a std::unordered_map to begin with. measuring showed it was about seventy percent of the cost
//of submitting an order, which is not surprising once you look at what it has to do: a node allocated
//for every resting order, a pointer chased to reach each one, and a rehash of the whole table every time
//the book grows past its load factor. an open addressing table keeps everything in one vector, so an
//insert writes into memory it already owns and a lookup usually touches one cache line.
//
//entries are removed by pulling later ones back into the gap rather than leaving a tombstone behind.
//tombstones would be simpler, but orders are cancelled and filled constantly here, and a table that
//never reclaims them slowly fills up with the dead and every lookup gets longer.
template <typename Value>
class OrderIndex {
    public:
        OrderIndex() {
            slots_.resize(initial_capacity);
        }

        //adds an order, or replaces where an order of the same id is being held
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

        //returns where the order is held, or nullptr if the table has never heard of it
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

        //removes an order, and reports whether there was one to remove
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

            //walk forward over the run of entries that follows. an entry has to be pulled back into the
            //gap unless its own bucket sits between the gap and where the entry currently is, because
            //moving one of those would put it before its bucket and a search would never find it
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

    private:
        struct Slot {
            OrderId id = 0;
            Value value{};
            bool occupied = false;
        };

        //big enough that a small book never grows the table, small enough to be free to construct
        static constexpr std::size_t initial_capacity = 64;

        //growing at seven tenths full keeps the runs of occupied slots short enough that a lookup
        //rarely walks more than a couple of them
        static constexpr std::size_t maximum_load_percent = 7;

        std::vector<Slot> slots_;
        std::size_t occupied_ = 0;

        //order ids usually arrive in sequence, and taking them modulo the table size would then drop
        //them into neighbouring slots and produce one long run. multiplying by the odd number nearest
        //the golden ratio and keeping the top bits spreads consecutive ids across the whole table
        std::size_t bucket(OrderId id) const {
            std::uint64_t mixed = static_cast<std::uint64_t>(id) * 0x9E3779B97F4A7C15ull;
            return static_cast<std::size_t>(mixed >> shift_);
        }

        //the table size is always a power of two, so wrapping round is a mask rather than a division
        std::size_t next(std::size_t index) const {
            return (index + 1) & (slots_.size() - 1);
        }

        void grow() {
            std::vector<Slot> old_slots = std::move(slots_);

            slots_.clear();
            slots_.resize(old_slots.size() * 2);
            shift_--;
            occupied_ = 0;

            for (const Slot& slot : old_slots) {
                if (slot.occupied) {
                    insert(slot.id, slot.value);
                }
            }
        }

        //how far down the mixed hash to shift to land inside the table. 64 minus log2 of the capacity
        std::size_t shift_ = 58;
};
