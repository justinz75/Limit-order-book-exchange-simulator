import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from pathlib import Path

root = Path(__file__).resolve().parent.parent
experiments_dir = root / "data" / "experiments"
plots_dir = Path(__file__).resolve().parent / "plots"
plots_dir.mkdir(exist_ok=True)

#part one: does the imbalance in the book say which way the price goes next

#how far ahead to look, counted in orders
horizons = [1, 10, 50, 200]

#the horizon the decile plot is drawn for
plot_horizon = 50

#fewer price moves than this in a sample and a correlation is not reported at all
minimum_moves = 30

datasets = {
    "original model": root / "data" / "simulation.csv",
    "noise only": experiments_dir / "noise_only.csv",
    "with informed traders": experiments_dir / "informed.csv",
    "informed, five times as active": experiments_dir / "informed_heavy.csv",
}

def load_book(path):
    df = pd.read_csv(path)

    #the book is only written out on order rows, so the trades are dropped
    book = df[df["event_type"] == "NEW_ORDER"].reset_index(drop=True)

    total = book["bid_depth"] + book["ask_depth"]
    book["imbalance"] = (book["bid_depth"] - book["ask_depth"]) / total.where(total > 0)

    #the move is measured forward from each row, so the imbalance only ever uses what was known then
    for horizon in horizons:
        book[f"move_{horizon}"] = book["mid_price"].shift(-horizon) - book["mid_price"]

    return book

def correlation_and_t(x, y):
    r = np.corrcoef(x, y)[0, 1]
    n = len(x)
    t = r * np.sqrt((n - 2) / (1 - r * r))
    return r, t, n

books = {name: load_book(path) for name, path in datasets.items()}

print("Does book imbalance predict the next move in the mid price?", flush=True)
print("imbalance is (bid depth - ask depth) / (bid depth + ask depth), over the top 5 levels", flush=True)
print("", flush=True)
print(f"{'':34}{'horizon':>9}{'correlation':>14}{'t':>9}{'samples':>10}{'moves':>8}", flush=True)

for name, book in books.items():
    for horizon in horizons:
        pairs = book[["imbalance", f"move_{horizon}"]].dropna()

        #moves over overlapping stretches share most of their path, so neighbouring samples are not
        #independent and a t statistic taken over all of them would look far more certain than it is.
        #taking every horizon-th row leaves stretches that do not overlap, which is what the t uses
        separate = pairs.iloc[::horizon]

        #a correlation needs the price to actually move. an early version of these experiments had a
        #price that almost never moved at all, and a handful of moves among sixty thousand rows gave a
        #correlation that looked enormous and meant nothing. so the moves are counted before anything
        moves = int((separate[f"move_{horizon}"] != 0).sum())

        if moves < minimum_moves:
            print(f"{name:34}{horizon:>9}{'too few moves':>23}{len(separate):>10}{moves:>8}", flush=True)
            continue

        r, t, n = correlation_and_t(separate["imbalance"], separate[f"move_{horizon}"])

        print(f"{name:34}{horizon:>9}{r:>14.3f}{t:>9.1f}{n:>10}{moves:>8}", flush=True)
    print("", flush=True)

plt.figure()

for name, book in books.items():
    pairs = book[["imbalance", f"move_{plot_horizon}"]].dropna()

    #ranked first so that the ten groups come out the same size even where imbalance values repeat
    deciles = pd.qcut(pairs["imbalance"].rank(method="first"), 10, labels=False)
    means = pairs.groupby(deciles)[f"move_{plot_horizon}"].mean()

    plt.plot(means.index + 1, means.values, marker="o", label=name)

plt.axhline(0, color="grey", linewidth=0.8)
plt.xlabel("Imbalance decile (1 = most ask heavy, 10 = most bid heavy)")
plt.ylabel(f"Mean mid move over the next {plot_horizon} orders (ticks)")
plt.title("Book Imbalance and the Next Price Move")
plt.legend()
plt.savefig(plots_dir / "imbalance_signal.png")
plt.close()

#part two: the market maker

summary = pd.read_csv(experiments_dir / "summary.csv")

maker_runs = {
    "maker": "no informed traders",
    "maker_informed": "facing informed traders",
    "maker_informed_no_skew": "informed, not leaning on position",
}

columns = ["maker_fills", "maker_pnl", "maker_edge", "maker_inventory_pnl", "maker_inventory_rms"]
maker_summary = summary[summary["experiment"].isin(maker_runs)]
totals = maker_summary.groupby("experiment")[columns].agg(["mean", "std"])

print("Market maker, mean and standard deviation over seeds", flush=True)
print("", flush=True)
print(f"{'':36}{'fills':>14}{'P&L':>18}{'spread earned':>18}{'lost on position':>20}{'position rms':>16}",
      flush=True)

for experiment, label in maker_runs.items():
    row = totals.loc[experiment]
    cells = [f"{row[(c, 'mean')]:.0f} +- {row[(c, 'std')]:.0f}" for c in columns[:4]]
    cells.append(f"{row[('maker_inventory_rms', 'mean')]:.1f} +- {row[('maker_inventory_rms', 'std')]:.1f}")
    print(f"{label:36}{cells[0]:>14}{cells[1]:>18}{cells[2]:>18}{cells[3]:>20}{cells[4]:>16}", flush=True)

print("", flush=True)

#where the maker's profit comes from, for the two runs that differ only in whether anybody is informed
compared = ["maker", "maker_informed"]
positions = np.arange(len(compared))
width = 0.35

plt.figure()
plt.bar(positions - width / 2,
        [totals.loc[e, ("maker_edge", "mean")] for e in compared], width,
        yerr=[totals.loc[e, ("maker_edge", "std")] for e in compared],
        capsize=4, label="Spread earned")
plt.bar(positions + width / 2,
        [totals.loc[e, ("maker_inventory_pnl", "mean")] for e in compared], width,
        yerr=[totals.loc[e, ("maker_inventory_pnl", "std")] for e in compared],
        capsize=4, label="Made or lost on position")
plt.axhline(0, color="grey", linewidth=0.8)
plt.xticks(positions, [maker_runs[e] for e in compared])
plt.ylabel("Ticks x quantity, mean over seeds")
plt.title("Where the Market Maker's P&L Comes From")
plt.legend()
plt.savefig(plots_dir / "maker_pnl.png")
plt.close()

#markouts: how the price moved after each fill, from the maker's side of it. a buy did well if the mid
#went up afterwards and a sell if it went down. at zero it is just the spread earned, and how far it falls
#from there as the horizon grows is how much the traders who filled the maker knew

#how far after a fill to look, counted in events
markout_horizons = [0, 1, 10, 100, 1000]

def mid_by_event(events_path):
    df = pd.read_csv(events_path)
    rows = df[df["event_type"] == "NEW_ORDER"][["event", "mid_price"]].dropna()

    #the last mid written in each event, carried forward across events that wrote none, so there is a
    #mid for every event number
    by_event = rows.groupby("event")["mid_price"].last()
    return by_event.reindex(range(1, int(by_event.index.max()) + 1)).ffill()

def average_markouts(name):
    fills = pd.read_csv(experiments_dir / f"{name}_fills.csv")
    if fills.empty:
        return {horizon: np.nan for horizon in markout_horizons}

    mids = mid_by_event(experiments_dir / f"{name}.csv")
    sign = np.where(fills["side"] == "BUY", 1.0, -1.0)
    price = fills["price"].to_numpy(dtype=float)
    quantity = fills["quantity"].to_numpy(dtype=float)

    results = {}
    for horizon in markout_horizons:
        if horizon == 0:
            later = fills["mid_before"].to_numpy(dtype=float)
        else:
            later = mids.reindex(fills["event"] + horizon).to_numpy(dtype=float)

        per_unit = sign * (later - price)
        valid = ~np.isnan(per_unit)
        results[horizon] = np.average(per_unit[valid], weights=quantity[valid]) if valid.any() else np.nan

    return results

print("Average markout per unit filled, in ticks, by events after the fill", flush=True)
print(f"{'':36}" + "".join(f"{h:>9}" for h in markout_horizons), flush=True)

plt.figure()

for name in compared:
    markouts = average_markouts(name)
    print(f"{maker_runs[name]:36}" + "".join(f"{markouts[h]:>9.3f}" for h in markout_horizons), flush=True)

    #zero cannot go on a log axis, so the horizons are drawn one step along each
    plt.plot(range(len(markout_horizons)), [markouts[h] for h in markout_horizons],
             marker="o", label=maker_runs[name])

print("", flush=True)

plt.axhline(0, color="grey", linewidth=0.8)
plt.xticks(range(len(markout_horizons)), [str(h) for h in markout_horizons])
plt.xlabel("Events after the fill")
plt.ylabel("Average markout per unit (ticks)")
plt.title("Market Maker Markouts")
plt.legend()
plt.savefig(plots_dir / "maker_markouts.png")
plt.close()

#the maker's position over one run, leaning on it and not
plt.figure()

#the one that does not lean is drawn first, so the one that does, which stays near zero, sits on top
position_labels = {
    "maker_informed_no_skew": "not leaning on its position",
    "maker_informed": "leaning on its position",
}

for name, label in position_labels.items():
    log = pd.read_csv(experiments_dir / f"{name}_maker.csv")
    plt.plot(log["event"], log["inventory"], linewidth=0.7, label=label)

plt.axhline(0, color="grey", linewidth=0.8)
plt.xlabel("Event")
plt.ylabel("Position")
plt.title("Market Maker Position Over Time")
plt.legend()
plt.savefig(plots_dir / "maker_position.png")
plt.close()
