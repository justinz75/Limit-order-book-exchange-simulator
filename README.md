# Limit order book exchange simulator

A matching engine for a single instrument, written in C++17, together with a simulator that pushes
random order flow through it and writes every event to CSV so the resulting market can be looked at in
pandas.

The engine handles limit and market orders under price-time priority, partial fills, cancels, and level
two depth queries. The simulator is what makes it interesting to look at: it generates a reference price
that wanders, places orders around it, and cancels a good share of them, which is enough to produce a
book that behaves roughly like a real one.

## Building

Needs CMake 3.15 or newer and a C++17 compiler.

```
cmake -S . -B build
cmake --build build --config Release
```

To run the tests:

```
cd build
ctest -C Release --output-on-failure
```

Both test binaries can also be run directly, and print how many checks passed.

## Running

The simulator writes to `data/simulation.csv`, and that path is relative, so run it from the repository
root:

```
./build/simulator
```

On a multi-config generator such as Visual Studio the binary is at `build/Release/simulator.exe`
instead. It runs 100,000 events and prints a summary:

```
Events: 100000
Orders submitted: 60039
Buy orders: 30025
Sell orders: 30014
Market orders: 6106
Cancel attempts: 39961
Successful cancels: 19760
Self trade cancellations: 39
Trades: 38150
Traded quantity: 211739
Average trade price: 102.302
```

Then the plots and statistics:

```
python analysis/analysis.py
```

which needs pandas and matplotlib, and writes into `analysis/plots`.

## How the book is put together

Each side is a `std::map` from price to a list of the orders resting at that price.

The two sides are ordered differently. Asks use the default ascending order, and bids are declared with
`std::greater`, so on both sides `begin()` is the best price and iterating forwards walks away from the
touch. That saves having to remember which end of which container is the interesting one, and it is easy
to get wrong: an early version read the bid side from the wrong end and quietly reported the *lowest*
bid as the best bid for a while, which is what test 5 in `tests/order_book_tests.cpp` now pins down.

Within a price level the orders sit in arrival order and matching always takes from the front, which is
what gives time priority. They are not held in a container of their own though. Every resting order in
the book lives in one `std::vector`, and each level threads its own queue through that vector by index,
so a level is just a head and a tail. `OrderBook` keeps an index from order id to the slot holding that
order, which is what makes a cancel a hash lookup and a few index writes rather than a scan of the
level. That index is `include/order_index.hpp`, an open addressing hash table written for the job after
measuring showed a `std::unordered_map` was most of what a submit cost; the performance section has the
numbers. Slots left behind by orders that trade or are cancelled go on a free list and are handed
out again, so a book that stays roughly the same size over time stops allocating.

Each level also keeps a running total of the quantity resting at it, kept up to date as orders join,
leave and are partly filled, so reading the depth does not mean walking the queue. That turned out to
matter considerably more than the arena did, which is covered below.

Trades execute at the resting order's price rather than the incoming one's, so the arriving order gets
the price improvement when it crosses further than it needed to.

Market orders share the matching loop with limit orders. The only differences are that the price check
is skipped, so they keep taking levels until they are filled or the other side runs out, and that
whatever they cannot fill is thrown away instead of resting, since a market order has no price to rest
at.

On top of that an order carries a time in force, saying how long it is willing to wait. Good till
cancelled is the default and behaves as above. Immediate or cancel takes whatever is there and gives up
on the rest instead of resting. Fill or kill is the awkward one, because it has to trade all of its
quantity or none of it, and that cannot be decided while matching: by the time you discover there was
not enough, you have already traded some of it and cannot honestly take it back. So the book is asked
what it could fill first, and the order is turned away before anything has changed if the answer is not
enough.

No trader trades with themselves. When an incoming order reaches a resting order under the same trader
id, that resting order is pulled and the incoming one carries on to whatever was queued behind it. This
is why the fill or kill check walks the individual orders rather than reading the running total each
level keeps: quantity belonging to the incoming trader is not really available to it, and counting it
would promise a fill that then could not happen.

A resting order can also be modified. Reducing its quantity happens where it already sits, so it keeps
its turn in the queue, which takes nothing away from anyone behind it. A new price, or more quantity,
gives that place up and goes to the back, which is what a venue does, and can trade on the way in if the
new price now crosses.

## Performance

`benchmarks/benchmark.cpp` puts a million orders through the book and times each operation on its own,
five times over, reporting the median with the range beside it. A single timing moves around by more
than the differences that are worth measuring, which is worth knowing before reading anything into one.

Measured on an AMD Ryzen 7 7435HS with MSVC 19.44. Everything quoted below was built with the same flags,
`/MD /O2 /Ob2 /DNDEBUG`, for reasons that the last part of this section explains.

```
submit                    3.44 M ops/s    290.7 ns/op   (281.9 to 294.8)
cancel                    8.54 M ops/s    117.0 ns/op   ( 99.1 to 141.4)
best bid                355.30 M ops/s      2.8 ns/op   (  2.8 to   2.8)
bid depth, 5 deep         3.31 M ops/s    301.9 ns/op   (301.8 to 303.8)
```

23% of the submitted orders crossed and had to be matched; the rest came to rest in the book.

Two things about reading these. The bracketed ranges are the spread within one run of the benchmark and
they understate how much the numbers move between runs: an early set of readings here came out near
345 ns for submit purely because something else on the machine was busy at the time. And the depth
figure is mostly not the lookup at all, since `bid_depth` returns a vector and a million calls is a
million allocations, which is most of what is being timed.

Adding the time in force field grew `Order` from 48 bytes to 56, and the arena node with it, which in a
workload this cache bound looked like it should cost something. Interleaving five runs of each build put
submit at 292 ns before and 295 ns after, and cancel at 105 ns and 108 ns, so whatever it costs is
smaller than the machine's own variation. Grouping the three small enums together would bring `Order`
back to 48 bytes, but on this evidence there is nothing there to win yet.

### How it got there

Two things changed since the first working version, and they are worth keeping apart, because only one
of them did much.

| | list per level, `unordered_map` index | shared arena | arena and flat index |
|---|---|---|---|
| submit | 474.0 ns | 415.2 ns | 290.7 ns |
| cancel | 293.3 ns | 211.5 ns | 117.0 ns |
| best bid | 2.7 ns | 2.9 ns | 2.8 ns |
| bid depth, 5 deep | 23.6 ms | 309.2 ns | 301.9 ns |
| full 100,000 event run | 0.88 s | 0.89 s | 0.48 s |

Moving the orders out of a list at each level and into one shared arena took an allocation off the
submit path and was worth about 12% there and 28% on cancel. The depth column is not a speedup so much
as a change of complexity: the old version worked out the quantity at a level by walking every order
resting there, and this benchmark leaves 50,000 orders at a level, so it was reading a quarter of a
million orders per call. Each level keeps a running total now.

None of which moved the end to end run at all. That is the honest result and it is easy to see why in
hindsight: the simulated book holds about twenty orders at a level, so walking one was never expensive,
and the 23 ms is a worst case the benchmark constructs rather than anything the simulation produces.

What did move it was the order index. Submitting was spending most of its time in the
`std::unordered_map` from order id to position, which allocates a node for every resting order, chases a
pointer to reach each one, and rehashes the lot whenever the book outgrows its load factor. Taking the
index out of the build altogether dropped submit from 415 ns to 133 ns, which is what said it was worth
attacking at all. Pre-sizing it so that it never rehashed got 415 ns to 337 ns, so about a third of the
cost was rehashing and the rest was the node per entry and the pointer chase to reach it.

`include/order_index.hpp` replaces it with an open addressing table living in one flat array: nothing
allocated per entry, and a lookup that usually touches a single cache line. Entries are removed by
pulling later ones back into the gap rather than by leaving a tombstone, because orders here are
cancelled and filled constantly and a table that never reclaims its dead slowly fills up with them. That
is the change that took the full run from 0.89 s to 0.48 s.

### A measurement mistake worth recording

An earlier version of this section said the arena took the full run from 1.18 s to 0.55 s. It did not.
That comparison had been built with `/MT` on one side and `/MD` on the other, and the two runtimes do
not use the same allocator, which in a workload that allocates this heavily was most of what was being
measured. Rebuilt with matching flags, the arena changed the end to end time by nothing at all.

It is an easy mistake to make when the two versions are built by different means, and it flattered the
change I happened to be making at the time, which is exactly when a result deserves a second look.

### What is left

290 ns for a submit is still not fast. The index is better but it is not free: with three quarters of a
million orders resting, the table is tens of megabytes, so an insert lands on a cache line nothing has
touched recently and pays for the miss however the table is laid out.

The remaining standard step is to lean on prices being bounded and swap the level map for a flat array
indexed by tick, which turns a level lookup into an array index and puts neighbouring levels next to
each other in memory. That is not done, and on the evidence above I would want to measure it before
assuming it helps.

## The simulation

Each event either submits a new order or cancels a resting one. New orders take their price from a
reference price that random walks by a small amount every event, with buys placed at or below it and
sells at or above it, at an offset drawn from a geometric distribution so most of them arrive near the
touch. One order in ten arrives as a market order.

Two of the parameters matter more than they look.

The reference price has to actually move. An earlier version pulled it back toward 100, which sounds
more realistic but means the price never travels far enough to reach orders resting a few ticks away, so
those orders sit there forever and the book grows without bound. Letting it wander freely is what lets
old orders be reached and traded.

The cancel rate has to be high. Orders arrive faster than they trade, and a cancel event removes at most
one order, so if cancels are rare the book only ever grows. At 40% the arrival and removal rates roughly
balance and depth settles at a steady level after a warm-up of around 30,000 events, which is what the
depth plot shows. Real venues cancel a far larger share of orders than this, so 40% is on the
conservative side.

## What the analysis shows

Over the 100,000 event run:

| | |
|---|---|
| VWAP | 102.30 |
| Mean spread | 1.04 ticks |
| Mid price range | 96.5 to 107.5 |
| Steady state depth | about 1,000 per side across the top 5 levels |
| Mean book imbalance | -0.013 |
| Market order share | 10.2% |

The imbalance sitting near zero and the two sides of the depth plot tracking each other are what you
would expect from a model with no directional pressure in it, since buys and sells arrive with equal
probability. The mid price wanders because the reference price does.

## Layout

```
include/        order, trade, price, order index and order book headers
src/            the matching engine and the simulator
data/           the csv writer, and the simulation output
tests/          the two test suites and the small test helper they share
benchmarks/     the throughput benchmark
analysis/       the pandas script and the plots it writes
```

## What is not in here

Nothing is threaded, and the book is not safe to touch from more than one thread. There is no wire
protocol and no persistence, so the book exists only for as long as the process does.

The simulated traders have no view of the market and no inventory, so there is no informed flow and no
market making, which is the main reason the price path is a plain random walk rather than anything with
structure to it. The simulator also only ever sends good till cancelled limit orders and market orders,
so the immediate or cancel, fill or kill and modify paths are exercised by the tests rather than by the
simulation. Self trade prevention is the exception, since it applies to every match: it fired 39 times
over the run above, which is about one match in a thousand, as you would expect from a thousand traders
picking orders at random.
