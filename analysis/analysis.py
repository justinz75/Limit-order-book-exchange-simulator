import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from pathlib import Path

csv_path = Path(__file__).resolve().parent.parent / "data" / "simulation.csv"
plots_dir = Path(__file__).resolve().parent / "plots"
plots_dir.mkdir(exist_ok=True)
df = pd.read_csv(csv_path)

print(df.head(), flush=True)
print(df.describe(), flush=True)

df.info()
print(df.isna().sum(), flush=True)

#the market data columns are only filled in on order rows, so the trades are dropped before plotting
book = df[df["event_type"] == "NEW_ORDER"]

#the raw series jump about too much to read over this many events, so the slower moving ones are averaged
smoothing_window = 500

#plot spread
plt.figure()
plt.plot(book["event"], book["spread"].rolling(smoothing_window).mean())
plt.xlabel("Event")
plt.ylabel("Bid-Ask Spread")
plt.title("Bid-Ask Spread Over Time")
plt.savefig(plots_dir / "spread_over_time.png")
plt.close()

#plot mid price
plt.figure()
plt.plot(book["event"], book["mid_price"], linewidth=0.7)
plt.xlabel("Event")
plt.ylabel("Mid Price")
plt.title("Mid Price Over Time")
plt.savefig(plots_dir / "mid_price_over_time.png")
plt.close()

#plot book depth
plt.figure()
plt.plot(book["event"], book["bid_depth"].rolling(smoothing_window).mean(), label="Bid Depth")
plt.plot(book["event"], book["ask_depth"].rolling(smoothing_window).mean(), label="Ask Depth")
plt.xlabel("Event")
plt.ylabel("Resting Quantity (Top 5 Levels)")
plt.title("Order Book Depth Over Time")
plt.legend()
plt.savefig(plots_dir / "book_depth_over_time.png")
plt.close()

#calculate order book imbalance
depth = df.loc[df["event_type"] == "NEW_ORDER", ["bid_depth", "ask_depth"]]
total_depth = depth["bid_depth"] + depth["ask_depth"]
imbalance = ((depth["bid_depth"] - depth["ask_depth"]) / total_depth.where(total_depth > 0)).dropna()
print("Mean book imbalance:", imbalance.mean(), flush=True)

#calculate market order share
new_orders = df[df["event_type"] == "NEW_ORDER"]
print("Market order share:", (new_orders["order_type"] == "MARKET").mean(), flush=True)

#calculate trade volume
trade_volume = df.loc[df["event_type"] == "TRADE","quantity"].sum()
print("Total traded volume:", trade_volume, flush=True)

#trade size distribution
trade_quantities = df.loc[df["event_type"] == "TRADE","quantity"]

plt.figure()
plt.hist(trade_quantities)
plt.xlabel("Trade Quantity")
plt.ylabel("Frequency")
plt.title("Distribution of Trade Sizes")
plt.savefig(plots_dir / "trade_size_distribution.png")
plt.close()

#VWAP calculation
trades = df[df["event_type"] == "TRADE"]
total_quantity = trades["quantity"].sum()
if total_quantity > 0:
    vwap = ((trades["price"] * trades["quantity"]).sum()/ total_quantity)
    print("VWAP:", vwap, flush=True)
else:
    print("No trades occurred.", flush=True)

#calculate volatility
mid_prices = df["mid_price"].dropna()
returns = mid_prices.pct_change().dropna()
volatility = returns.std()
print("Price volatility:", volatility, flush=True)