#include "simulator.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

//each experiment runs as many events as the main simulation does
constexpr std::size_t number_of_events = 100000;

//and is repeated over this many seeds. one run of a market maker is one draw of its profit and loss,
//which on its own cannot say whether a change to it helped or not
constexpr int seeds_per_experiment = 10;
constexpr std::uint64_t first_seed = 50;

const std::string output_directory = "data/experiments";

struct Experiment {
    std::string name;
    SimulationConfig config;
};

//the experiments are built so that each one differs from another in a single thing, so that any
//difference between those two can be put down to that one thing and not to something else that moved
std::vector<Experiment> build_experiments() {
    //ordinary traders working from a view of the value that is a few hundred events old. the price still
    //moves, because their view does, but it lags, and the lag is what informed traders trade on
    SimulationConfig noise_only;
    noise_only.noise_view_lag = 300;

    SimulationConfig informed = noise_only;
    informed.informed_percentage = 10;

    //the same, with informed traders looking five times as often. this is here to test one explanation
    //for what the first informed run showed, and its result is reported whichever way it comes out
    SimulationConfig informed_heavy = noise_only;
    informed_heavy.informed_percentage = 50;

    SimulationConfig maker = noise_only;
    maker.market_maker = true;

    SimulationConfig maker_informed = maker;
    maker_informed.informed_percentage = 10;

    SimulationConfig maker_informed_no_skew = maker_informed;
    maker_informed_no_skew.maker_skew_per_unit = 0.0;

    return {
        {"noise_only", noise_only},
        {"informed", informed},
        {"informed_heavy", informed_heavy},
        {"maker", maker},
        {"maker_informed", maker_informed},
        {"maker_informed_no_skew", maker_informed_no_skew}
    };
}

void write_maker_log(const std::string& path, const Simulator& simulator) {
    std::ofstream file(path);
    file << "event,inventory,cash,mid,reference\n";

    for (const MakerSnapshot& snapshot : simulator.maker_snapshots()) {
        file << snapshot.event << ","
             << snapshot.inventory << ","
             << snapshot.cash << ","
             << snapshot.mid << ","
             << snapshot.reference << "\n";
    }
}

void write_maker_fills(const std::string& path, const Simulator& simulator) {
    std::ofstream file(path);
    file << "event,side,price,quantity,mid_before\n";

    for (const MakerFill& fill : simulator.maker_fills()) {
        file << fill.event << ","
             << (fill.side == Side::Buy ? "BUY" : "SELL") << ","
             << fill.price << ","
             << fill.quantity << ","
             << fill.mid_before << "\n";
    }
}

//the mean of a set of runs, and how widely they spread around it
struct Spread {
    double mean = 0.0;
    double deviation = 0.0;
};

Spread summarise(const std::vector<double>& values) {
    Spread result;

    if (values.empty()) {
        return result;
    }

    double sum = 0.0;
    for (double value : values) {
        sum += value;
    }
    result.mean = sum / static_cast<double>(values.size());

    double squares = 0.0;
    for (double value : values) {
        squares += (value - result.mean) * (value - result.mean);
    }

    //the sample deviation, since these runs are a sample of what could have happened
    if (values.size() > 1) {
        result.deviation = std::sqrt(squares / static_cast<double>(values.size() - 1));
    }

    return result;
}

std::string format(const Spread& spread, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << spread.mean << " +- " << spread.deviation;
    return out.str();
}

int main() {
    std::filesystem::create_directories(output_directory);

    std::ofstream summary(output_directory + "/summary.csv");
    summary << "experiment,seed,trades,informed_orders,maker_fills,maker_volume,"
            << "maker_pnl,maker_edge,maker_inventory_pnl,maker_inventory_rms,"
            << "maker_peak_inventory,maker_final_inventory\n";

    std::cout << "Experiments: " << number_of_events << " events each, repeated over "
              << seeds_per_experiment << " seeds, shown as mean +- standard deviation\n\n";

    std::cout << std::left << std::setw(24) << "experiment"
              << std::right
              << std::setw(18) << "trades"
              << std::setw(18) << "informed orders"
              << std::setw(20) << "maker P&L"
              << std::setw(20) << "spread earned"
              << std::setw(22) << "lost on position"
              << std::setw(16) << "position rms" << "\n";

    for (const Experiment& experiment : build_experiments()) {
        std::vector<double> trades;
        std::vector<double> informed_orders;
        std::vector<double> pnl;
        std::vector<double> edge;
        std::vector<double> inventory_pnl;
        std::vector<double> inventory_rms;

        for (int i = 0; i < seeds_per_experiment; ++i) {
            std::uint64_t seed = first_seed + static_cast<std::uint64_t>(i);

            //only the first seed writes out every event, since that is the run the analysis plots. the
            //rest only contribute their totals, and an empty name gives a writer that records nothing
            std::string events_path;
            if (i == 0) {
                events_path = output_directory + "/" + experiment.name + ".csv";
            }

            DataWriter writer(events_path);
            Simulator simulator(seed, experiment.config);
            SimulationStats stats = simulator.run(number_of_events, writer);

            if (i == 0 && experiment.config.market_maker) {
                write_maker_log(output_directory + "/" + experiment.name + "_maker.csv", simulator);
                write_maker_fills(output_directory + "/" + experiment.name + "_fills.csv", simulator);
            }

            summary << experiment.name << ","
                    << seed << ","
                    << stats.trades << ","
                    << stats.informed_orders << ","
                    << stats.maker_fills << ","
                    << stats.maker_volume << ","
                    << stats.maker_pnl << ","
                    << stats.maker_edge << ","
                    << stats.maker_inventory_pnl << ","
                    << stats.maker_inventory_rms << ","
                    << stats.maker_peak_inventory << ","
                    << stats.maker_final_inventory << "\n";

            trades.push_back(static_cast<double>(stats.trades));
            informed_orders.push_back(static_cast<double>(stats.informed_orders));
            pnl.push_back(stats.maker_pnl);
            edge.push_back(stats.maker_edge);
            inventory_pnl.push_back(stats.maker_inventory_pnl);
            inventory_rms.push_back(stats.maker_inventory_rms);
        }

        std::cout << std::left << std::setw(24) << experiment.name
                  << std::right
                  << std::setw(18) << format(summarise(trades), 0);

        if (experiment.config.informed_percentage > 0) {
            std::cout << std::setw(18) << format(summarise(informed_orders), 0);
        } else {
            std::cout << std::setw(18) << "-";
        }

        if (experiment.config.market_maker) {
            std::cout << std::setw(20) << format(summarise(pnl), 0)
                      << std::setw(20) << format(summarise(edge), 0)
                      << std::setw(22) << format(summarise(inventory_pnl), 0)
                      << std::setw(16) << format(summarise(inventory_rms), 1);
        } else {
            std::cout << std::setw(20) << "-"
                      << std::setw(20) << "-"
                      << std::setw(22) << "-"
                      << std::setw(16) << "-";
        }

        std::cout << "\n";
    }

    std::cout << "\nEvent files and the maker's fills and position are in " << output_directory
              << ", for analysis/experiments.py to read\n";

    return 0;
}
