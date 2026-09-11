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

#part one: does book imbalance predict the next price move

#how far ahead to look, counted in orders
horizons = [1, 10, 50, 200]

#the horizon the decile plot is drawn for
plot_horizon = 50

#skip a correlation when a sample has fewer price moves than this
minimum_moves = 30

datasets = {
    "original model": root / "data" / "simulation.csv",
    "noise only": experiments_dir / "noise_only.csv",
    "with informed traders": experiments_dir / "informed.csv",
    "informed, five times as active": experiments_dir / "informed_heavy.csv",
}

def load_book(path):
    df = pd.read_csv(path)

    #keep only the order rows, since trades have no book data
    book = df[df["event_type"] == "NEW_ORDER"].reset_index(drop=True)

    total = book["bid_depth"] + book["ask_depth"]
    book["imbalance"] = (book["bid_depth"] - book["ask_depth"]) / total.where(total > 0)

    #move in the mid over the next few orders from each row
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

        #use non-overlapping stretches so the t statistic is not inflated
        separate = pairs.iloc[::horizon]

        #count the price moves, since a correlation needs the price to actually move
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

    #rank first so the ten groups come out the same size
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

#part two: is the negative signal just the book being out of line with the value

stale_datasets = {
    "original model": ("original", 0),
    "noise only": ("noise_only", 300),
    "with informed traders": ("informed", 300),
    "informed, five times as active": ("informed_heavy", 300),
}

def residual(values, control):
    #what is left of values once the part explained by control is removed
    slope = np.cov(values, control)[0, 1] / np.var(control, ddof=1)
    return values - slope * control

print("Is the imbalance just the book being out of line with the value?", flush=True)
print(f"gap is the mid less the value the ordinary traders price from, moves are over {plot_horizon} orders",
      flush=True)
print("", flush=True)
print(f"{'':34}{'imbalance~gap':>15}{'gap~move':>11}{'imbalance~move':>16}{'t':>7}"
      f"{'gap held fixed':>16}{'t':>7}{'samples':>9}", flush=True)

for name, (experiment, lag) in stale_datasets.items():
    book = load_book(experiments_dir / f"{experiment}.csv")
    history = pd.read_csv(experiments_dir / f"{experiment}_history.csv").set_index("event")

    #the value the ordinary traders see, which is the reference lag events earlier
    view = history["reference"].shift(lag)
    book["gap"] = book["mid_price"] - book["event"].map(view)

    sample = book[["imbalance", "gap", f"move_{plot_horizon}"]].dropna().iloc[::plot_horizon]
    imbalance = sample["imbalance"].to_numpy()
    gap = sample["gap"].to_numpy()
    move = sample[f"move_{plot_horizon}"].to_numpy()
    n = len(sample)

    imbalance_gap = np.corrcoef(imbalance, gap)[0, 1]
    gap_move = np.corrcoef(gap, move)[0, 1]
    imbalance_move, t, _ = correlation_and_t(imbalance, move)

    held = np.corrcoef(residual(imbalance, gap), residual(move, gap))[0, 1]
    held_t = held * np.sqrt((n - 3) / (1 - held * held))

    print(f"{name:34}{imbalance_gap:>15.3f}{gap_move:>11.3f}{imbalance_move:>16.3f}{t:>7.1f}"
          f"{held:>16.3f}{held_t:>7.1f}{n:>9}", flush=True)

print("", flush=True)

#part three: the market maker

summary = pd.read_csv(experiments_dir / "summary.csv")

maker_runs = {
    "maker": "no informed traders",
    "maker_their_view": "no informed traders, sees their view",
    "maker_informed": "facing informed traders",
    "maker_informed_their_view": "facing informed, sees their view",
    "maker_informed_no_skew": "informed, not leaning on position",
}

columns = ["maker_fills", "maker_pnl", "maker_edge", "maker_inventory_pnl", "maker_inventory_rms"]
maker_summary = summary[summary["experiment"].isin(maker_runs)]
totals = maker_summary.groupby("experiment")[columns].agg(["mean", "std"])

print("Market maker, mean and standard deviation over seeds", flush=True)
print("", flush=True)
print(f"{'':38}{'fills':>14}{'P&L':>18}{'spread earned':>18}{'lost on position':>20}{'position rms':>16}",
      flush=True)

for experiment, label in maker_runs.items():
    row = totals.loc[experiment]
    cells = [f"{row[(c, 'mean')]:.0f} +- {row[(c, 'std')]:.0f}" for c in columns[:4]]
    cells.append(f"{row[('maker_inventory_rms', 'mean')]:.1f} +- {row[('maker_inventory_rms', 'std')]:.1f}")
    print(f"{label:38}{cells[0]:>14}{cells[1]:>18}{cells[2]:>18}{cells[3]:>20}{cells[4]:>16}", flush=True)

print("", flush=True)

#the four maker runs, with and without informed traders and on the mid or their view
compared = {
    "maker": "mid",
    "maker_their_view": "their view",
    "maker_informed": "mid,\ninformed",
    "maker_informed_their_view": "their view,\ninformed",
}

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
plt.xticks(positions, list(compared.values()))
plt.xlabel("What the maker centres its quotes on")
plt.ylabel("Ticks x quantity, mean over seeds")
plt.title("Where the Market Maker's P&L Comes From")
plt.legend()
plt.tight_layout()
plt.savefig(plots_dir / "maker_pnl.png")
plt.close()

#markouts, averaged over ten seeds: how the mid moved after each fill, from the maker's side

markout_horizons = [0, 1, 10, 100, 1000]
markout_columns = [f"maker_markout_{h}" for h in markout_horizons]
markouts = maker_summary.groupby("experiment")[markout_columns].agg(["mean", "std", "count"])

print("Average markout per unit filled, in ticks, by events after the fill, mean and standard deviation over "
      "seeds", flush=True)
print(f"{'':38}" + "".join(f"{h:>17}" for h in markout_horizons), flush=True)

for experiment, label in maker_runs.items():
    row = markouts.loc[experiment]
    cells = "".join(f"{row[(c, 'mean')]:.3f} +- {row[(c, 'std')]:.3f}".rjust(17) for c in markout_columns)
    print(f"{label:38}{cells}", flush=True)

print("", flush=True)

#change in a markout between two runs, and its standard error
def difference(first, second, column):
    a = markouts.loc[first]
    b = markouts.loc[second]
    change = b[(column, "mean")] - a[(column, "mean")]
    error = np.sqrt(a[(column, "std")] ** 2 / a[(column, "count")] + b[(column, "std")] ** 2 / b[(column, "count")])
    return change, error

comparisons = [
    ("maker", "maker_informed", "adding informed traders, maker on the mid"),
    ("maker_their_view", "maker_informed_their_view", "adding informed traders, maker on their view"),
    ("maker", "maker_their_view", "giving the maker their view, nobody informed"),
    ("maker_informed", "maker_informed_their_view", "giving the maker their view, informed present"),
]

print("Change in the markout a thousand events on", flush=True)

for first, second, label in comparisons:
    change, error = difference(first, second, "maker_markout_1000")
    print(f"  {label:48}{change:>+9.3f} +- {error:.3f}   ({change / error:+.1f} standard errors)", flush=True)

print("", flush=True)

plt.figure()

for experiment in compared:
    row = markouts.loc[experiment]
    means = [row[(c, "mean")] for c in markout_columns]

    #error bars are standard errors of the mean
    errors = [row[(c, "std")] / np.sqrt(row[(c, "count")]) for c in markout_columns]

    #evenly spaced horizons, since zero cannot go on a log axis
    plt.errorbar(range(len(markout_horizons)), means, yerr=errors, marker="o", capsize=3,
                 label=maker_runs[experiment])

plt.axhline(0, color="grey", linewidth=0.8)
plt.xticks(range(len(markout_horizons)), [str(h) for h in markout_horizons])
plt.xlabel("Events after the fill")
plt.ylabel("Average markout per unit (ticks)")
plt.title("Market Maker Markouts, Mean Over Ten Seeds")
plt.legend()
plt.savefig(plots_dir / "maker_markouts.png")
plt.close()

#the maker's position over one run, leaning on it and not
plt.figure()

#draw the one that does not lean first so the other stays visible on top
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
