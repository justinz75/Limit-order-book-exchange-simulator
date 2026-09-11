#include "simulator.hpp"

#include <algorithm>
#include <cmath>

Simulator::Simulator(std::uint64_t seed, SimulationConfig config)
    : config_(config),
      rng_(seed) {
}

const std::vector<MakerFill>& Simulator::maker_fills() const {
    return maker_fills_;
}

const std::vector<MakerSnapshot>& Simulator::maker_snapshots() const {
    return maker_snapshots_;
}

const std::vector<double>& Simulator::mid_history() const {
    return mid_history_;
}

const std::vector<double>& Simulator::reference_history() const {
    return reference_history_;
}

//the size of the random step the reference price takes each event
constexpr double reference_step_size = 0.03;

//the lowest the reference price is allowed to go
constexpr double minimum_reference_price = 1.0;

//how tightly orders cluster around the reference price, higher is tighter
constexpr double offset_tightness = 0.55;

//moves the reference price by a small random step
void Simulator::step_reference_price() {
    std::normal_distribution<double> noise(0.0, reference_step_size);
    reference_price_ += noise(rng_);

    if (reference_price_ < minimum_reference_price) {
        reference_price_ = minimum_reference_price;
    }

    if (config_.noise_view_lag > 0 || config_.record_history) {
        reference_history_.push_back(reference_price_);
    }
}

//most orders arrive at or near the reference price, with a thinner tail further out
Price Simulator::random_offset() {
    std::geometric_distribution<int> distribution(offset_tightness);
    return static_cast<Price>(distribution(rng_));
}

//returns the reference as it was noise_view_lag events ago
double Simulator::noise_view() const {
    if (config_.noise_view_lag > 0 && !reference_history_.empty()) {
        std::size_t back = std::min(config_.noise_view_lag, reference_history_.size() - 1);
        return reference_history_[reference_history_.size() - 1 - back];
    }

    return reference_price_;
}

//a buy is placed at or below the value and a sell at or above it
Price Simulator::random_price(Side side) {
    Price offset = random_offset();

    //ordinary traders price off their possibly out of date view of the value
    Price reference = static_cast<Price>(std::llround(noise_view()));

    if (side == Side::Buy) {
        return reference - offset;
    }

    return reference + offset;
}

//returns a random quantity for an order, which is a random integer between 1 and 20
Quantity Simulator::random_quantity() {
    std::uniform_int_distribution<Quantity> distribution(1, 20);
    return distribution(rng_);
}

//returns a random trader ID for an order, which is a random integer between 1 and 1000
std::uint64_t Simulator::random_trader_id() {
    std::uniform_int_distribution<std::uint64_t> distribution(1, 1000);
    return distribution(rng_);
}

//informed traders get their own range of trader ids, apart from the ordinary ones
TraderId Simulator::random_informed_trader_id() {
    std::uniform_int_distribution<TraderId> distribution(1001, 1100);
    return distribution(rng_);
}

//chooses a random order ID from the known order IDs, or 0 if there are none
OrderId Simulator::choose_order_to_cancel() {

    if (known_order_ids_.empty()) {
        return 0;
    }
    std::uniform_int_distribution<std::size_t> distribution(
        0,
        known_order_ids_.size() - 1
    );
    return known_order_ids_[distribution(rng_)];
}

//the share of events that cancel a resting order rather than submitting a new one
constexpr int cancel_percentage = 40;

//returns true if the next event should cancel a resting order rather than submit a new one
bool Simulator::should_cancel() {
    std::uniform_int_distribution<int> distribution(1, 100);
    return distribution(rng_) <= cancel_percentage;
}

//returns true if the next generated order should be a market order
bool Simulator::should_be_market() {
    std::uniform_int_distribution<int> distribution(1, 100);
    return distribution(rng_) <= config_.market_order_percentage;
}

//returns true if an informed trader should look at the book on this event
bool Simulator::should_be_informed() {
    std::uniform_int_distribution<int> distribution(1, 100);
    return distribution(rng_) <= config_.informed_percentage;
}

//generates a random order with a unique ID, random trader ID, side, type, price and quantity
Order Simulator::generate_order() {
    std::uniform_int_distribution<int> side_distribution(0, 1);
    Side side;

    //randomly choose the side of the order (buy or sell) using a uniform distribution
    if (side_distribution(rng_) == 0) {
        side = Side::Buy;
    } else {
        side = Side::Sell;
    }

    //generate a unique order ID, random trader ID, price, and quantity for the order
    OrderId order_id = next_order_id_++;
    std::uint64_t trader_id = random_trader_id();
    Price price = random_price(side);
    Quantity quantity = random_quantity();

    //market orders have no price of their own
    OrderType type = OrderType::Limit;
    if (should_be_market()) {
        type = OrderType::Market;
        price = 0;
    }

    //create and return the order object with the generated attributes
    Order order{
        order_id,
        trader_id,
        side,
        type,
        price,
        quantity,
        next_timestamp_++
    };
    return order;
}

//submits an order to the order book, records the resulting trades and updates the statistics
void Simulator::process_order(const Order& order, std::size_t event_number, DataWriter& writer) {
    //the mid when the order arrived, used for the maker's edge
    double mid_before = order_book_.mid_price().value_or(maker_center_);

    auto trades = order_book_.submit(order);
    stats_.orders_submitted++;

    if (order.side == Side::Buy) {
        stats_.buy_orders++;
    } else {
        stats_.sell_orders++;
    }

    if (order.type == OrderType::Market) {
        stats_.market_orders++;
    }

    writer.write_event(
        event_number,
        order,
        order_book_
    );

    Quantity filled = 0;

    for (const auto& trade : trades) {
        stats_.trades++;
        stats_.traded_quantity += trade.quantity;
        stats_.total_traded_value +=
            static_cast<double>(trade.price) *
            static_cast<double>(trade.quantity);

        filled += trade.quantity;

        writer.write_trade(
            event_number,
            trade
        );

        if (config_.market_maker) {
            record_maker_fill(trade, event_number, mid_before);
        }
    }

    //only a good till cancelled limit order with quantity left over can be cancelled later
    if (order.type == OrderType::Limit &&
        order.time_in_force == TimeInForce::GoodTillCancelled &&
        filled < order.remaining_quantity) {
        known_order_ids_.push_back(order.id);
    }
}

//an informed trader trades against whatever is mispriced, and otherwise does nothing
void Simulator::process_informed(std::size_t event_number, DataWriter& writer) {
    auto best_bid = order_book_.best_bid();
    auto best_ask = order_book_.best_ask();

    Side side;
    Price price;

    if (best_ask.has_value() &&
        static_cast<double>(best_ask.value()) < reference_price_ - config_.informed_threshold) {
        side = Side::Buy;

        //pays up to the reference price, less its threshold
        price = static_cast<Price>(std::floor(reference_price_ - config_.informed_threshold));
    } else if (best_bid.has_value() &&
               static_cast<double>(best_bid.value()) > reference_price_ + config_.informed_threshold) {
        side = Side::Sell;
        price = static_cast<Price>(std::ceil(reference_price_ + config_.informed_threshold));
    } else {
        return;
    }

    OrderId order_id = next_order_id_++;
    TraderId trader_id = random_informed_trader_id();
    Quantity quantity = random_quantity();

    Order order{
        order_id,
        trader_id,
        side,
        OrderType::Limit,
        price,
        quantity,
        next_timestamp_++,
        TimeInForce::ImmediateOrCancel
    };

    stats_.informed_orders++;
    process_order(order, event_number, writer);
}

//updates the market maker's quotes, leaving any whose price has not changed
void Simulator::requote_market_maker() {
    auto best_bid = order_book_.best_bid();
    auto best_ask = order_book_.best_ask();

    if (config_.maker_uses_noise_view) {
        //rounded to the nearest half tick, the grid a mid sits on
        maker_center_ = std::round(noise_view() * 2.0) / 2.0;
    } else if (best_bid.has_value() && best_ask.has_value()) {
        maker_center_ = (static_cast<double>(best_bid.value()) +
                         static_cast<double>(best_ask.value())) / 2.0;
    }

    //lean both quotes against the position to drift back toward flat
    double skew = -config_.maker_skew_per_unit * static_cast<double>(maker_inventory_);

    //round outward so both quotes sit the same distance from the centre
    Price wanted_bid = static_cast<Price>(std::floor(maker_center_ + skew - config_.maker_half_spread));
    Price wanted_ask = static_cast<Price>(std::ceil(maker_center_ + skew + config_.maker_half_spread));

    std::int64_t size = static_cast<std::int64_t>(config_.maker_quote_size);
    bool show_bid = maker_inventory_ + size <= config_.maker_inventory_limit;
    bool show_ask = maker_inventory_ - size >= -config_.maker_inventory_limit;

    //keep the bid a tick below the best offer so it only ever rests
    best_ask = order_book_.best_ask();
    if (best_ask.has_value()) {
        wanted_bid = std::min(wanted_bid, best_ask.value() - 1);
    }

    update_maker_quote(Side::Buy, show_bid, wanted_bid);

    //work out the offer after the bid so the two can never cross
    best_bid = order_book_.best_bid();
    if (best_bid.has_value()) {
        wanted_ask = std::max(wanted_ask, best_bid.value() + 1);
    }

    update_maker_quote(Side::Sell, show_ask, wanted_ask);
}

void Simulator::update_maker_quote(Side side, bool show, Price wanted) {
    OrderId& id = (side == Side::Buy) ? maker_bid_id_ : maker_ask_id_;
    Price& price = (side == Side::Buy) ? maker_bid_price_ : maker_ask_price_;
    Quantity& remaining = (side == Side::Buy) ? maker_bid_remaining_ : maker_ask_remaining_;

    //still resting at the right price, so it keeps its place in the queue
    if (show && id != 0 && price == wanted) {
        return;
    }

    if (id != 0) {
        order_book_.cancel_order(id);
        id = 0;
        remaining = 0;
    }

    if (!show) {
        return;
    }

    Order quote{
        next_order_id_++,
        maker_trader_id,
        side,
        OrderType::Limit,
        wanted,
        config_.maker_quote_size,
        next_timestamp_++
    };

    order_book_.submit(quote);

    id = quote.id;
    price = wanted;
    remaining = config_.maker_quote_size;
}

//updates the maker's cash, position and edge for a fill
void Simulator::record_maker_fill(const Trade& trade, std::size_t event_number, double mid_before) {
    Side maker_side;

    if (trade.buyer_id == maker_trader_id) {
        maker_side = Side::Buy;
    } else if (trade.seller_id == maker_trader_id) {
        maker_side = Side::Sell;
    } else {
        return;
    }

    double price = static_cast<double>(trade.price);
    double quantity = static_cast<double>(trade.quantity);

    if (maker_side == Side::Buy) {
        maker_inventory_ += static_cast<std::int64_t>(trade.quantity);
        maker_cash_ -= price * quantity;
        stats_.maker_edge += (mid_before - price) * quantity;
    } else {
        maker_inventory_ -= static_cast<std::int64_t>(trade.quantity);
        maker_cash_ += price * quantity;
        stats_.maker_edge += (price - mid_before) * quantity;
    }

    if (trade.resting_order_id == maker_bid_id_) {
        maker_bid_remaining_ -= trade.quantity;

        if (maker_bid_remaining_ == 0) {
            maker_bid_id_ = 0;
        }
    } else if (trade.resting_order_id == maker_ask_id_) {
        maker_ask_remaining_ -= trade.quantity;

        if (maker_ask_remaining_ == 0) {
            maker_ask_id_ = 0;
        }
    }

    stats_.maker_fills++;
    stats_.maker_volume += trade.quantity;

    maker_fills_.push_back({event_number, maker_side, trade.price, trade.quantity, mid_before});
}

//average markout per unit, leaving out fills too near the end for the horizon
double Simulator::average_markout(std::size_t horizon) const {
    double weighted = 0.0;
    double units = 0.0;

    for (const MakerFill& fill : maker_fills_) {
        double later = fill.mid_before;

        if (horizon > 0) {
            //the mid at the end of event e is kept at index e - 1
            std::size_t index = fill.event + horizon - 1;

            if (index >= mid_history_.size()) {
                continue;
            }

            later = mid_history_[index];
        }

        double sign = (fill.side == Side::Buy) ? 1.0 : -1.0;
        double quantity = static_cast<double>(fill.quantity);

        weighted += sign * (later - static_cast<double>(fill.price)) * quantity;
        units += quantity;
    }

    if (units == 0.0) {
        return 0.0;
    }

    return weighted / units;
}

//cancels a randomly chosen known order and updates the statistics
void Simulator::process_cancel() {
    //if there are no known order IDs, return early since there are no orders to cancel
    if (known_order_ids_.empty()) {
        return;
    }

    OrderId order_id = choose_order_to_cancel();
    stats_.cancel_attempts++;

    //attempt to cancel the order in the order book and update statistics accordingly
    if (order_book_.cancel_order(order_id)) {
        stats_.successful_cancels++;
    }

    //forget the order either way, since a failed cancel means it was already filled
    known_order_ids_.erase(
        std::remove(
            known_order_ids_.begin(),
            known_order_ids_.end(),
            order_id
        ),
        known_order_ids_.end()
    );
}

double SimulationStats::average_trade_price() const {
    //calculate the average trade price by dividing the total traded value by the total traded quantity
    if (traded_quantity == 0) {
        return 0.0;
    }
    return total_traded_value / static_cast<double>(traded_quantity);
}

//runs the simulation for a specified number of events, generating random orders and processing them through the order book and collects statistics about the simulation
SimulationStats Simulator::run(std::size_t number_of_events, DataWriter& writer) {
    stats_ = SimulationStats{};
    for (std::size_t i = 0; i < number_of_events; ++i) {
        std::size_t event_number = i + 1;

        //the market drifts a little between events, whether or not an order arrives
        step_reference_price();

        //the maker updates its quotes before anything else happens
        if (config_.market_maker) {
            requote_market_maker();
        }

        if (should_cancel() && !known_order_ids_.empty()) {
            process_cancel();

        } else {
            Order order = generate_order();
            process_order(order, event_number, writer);
        }

        //informed traders act on top of the ordinary flow rather than replacing any of it
        if (config_.informed_percentage > 0 && should_be_informed()) {
            process_informed(event_number, writer);
        }

        //record the mid, reusing the last one if a side of the book is empty
        if (config_.record_history) {
            double fallback = mid_history_.empty() ? reference_price_ : mid_history_.back();
            mid_history_.push_back(order_book_.mid_price().value_or(fallback));
        }

        if (config_.market_maker) {
            double inventory = static_cast<double>(maker_inventory_);
            inventory_square_sum_ += inventory * inventory;

            std::int64_t magnitude = maker_inventory_ < 0 ? -maker_inventory_ : maker_inventory_;
            stats_.maker_peak_inventory = std::max(stats_.maker_peak_inventory, magnitude);

            if (config_.maker_log_interval > 0 && event_number % config_.maker_log_interval == 0) {
                maker_snapshots_.push_back({
                    event_number,
                    maker_inventory_,
                    maker_cash_,
                    order_book_.mid_price().value_or(maker_center_),
                    reference_price_
                });
            }
        }
    }

    //collect the self trade cancellations the book counted
    stats_.self_trade_cancellations = order_book_.self_trade_cancellations();

    if (config_.market_maker && number_of_events > 0) {
        double final_mid = order_book_.mid_price().value_or(maker_center_);

        stats_.maker_final_inventory = maker_inventory_;
        stats_.maker_pnl = maker_cash_ + static_cast<double>(maker_inventory_) * final_mid;
        stats_.maker_inventory_pnl = stats_.maker_pnl - stats_.maker_edge;
        stats_.maker_inventory_rms =
            std::sqrt(inventory_square_sum_ / static_cast<double>(number_of_events));

        if (config_.record_history) {
            stats_.maker_markout_0 = average_markout(0);
            stats_.maker_markout_1 = average_markout(1);
            stats_.maker_markout_10 = average_markout(10);
            stats_.maker_markout_100 = average_markout(100);
            stats_.maker_markout_1000 = average_markout(1000);
        }
    }

    return stats_;
}
