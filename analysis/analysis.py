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

#plot spread
sample = df.iloc[:5000]

plt.figure()
plt.plot(sample["event"], sample["spread"])
plt.xlabel("Event")
plt.ylabel("Bid-Ask Spread")
plt.title("Bid-Ask Spread Over Time")
plt.savefig(plots_dir / "spread_over_time.png")
plt.close()

#plot mid price
sample = df.iloc[:5000]

plt.figure()
plt.plot(sample["event"], sample["mid_price"])
plt.xlabel("Event")
plt.ylabel("Mid Price")
plt.title("Mid Price Over Time")
plt.savefig(plots_dir / "mid_price_over_time.png")
plt.close()

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