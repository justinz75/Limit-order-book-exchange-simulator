# Limit order book exchange simulator

[![CI](https://github.com/justinz75/Limit-order-book-exchange-simulator/actions/workflows/ci.yml/badge.svg)](https://github.com/justinz75/Limit-order-book-exchange-simulator/actions/workflows/ci.yml)

A matching engine for a single instrument, written in C++17, together with a simulator that pushes
random order flow through it and writes every event to CSV so the resulting market can be looked at in
pandas.

The engine handles limit and market orders under price-time priority, with partial fills, cancels,
modifies, immediate or cancel and fill or kill orders, self trade prevention, and level two depth queries.
The simulator is what makes it interesting to look at: it generates a reference price that wanders, places
orders around it, and cancels a good share of them, which is enough to produce a book that behaves roughly
like a real one.

## Highlights

- **3.51 million submits a second** on one thread, and 7.90 million cancels, on a Ryzen 7 7435HS.
  See [Performance](#performance).
- **p99.9 submit latency down from 4,448 ns to 1,232 ns** by sizing the book up front and writing to every
  page before the run starts. See [Sizing the book up front](#sizing-the-book-up-front).
- **The full 100,000 event run down from 0.89 s to 0.48 s** by replacing `std::unordered_map` with an open
  addressing hash table written for the job. See [How it got there](#how-it-got-there).
- **A market maker that hands back 78% of the spread it earns** to adverse selection even with nobody
  informed in the market, and whose typical position falls from 113 to 4.9 once it leans its quotes on
  position. See [The market maker](#the-market-maker).
- **Tested on three compilers**: unit tests for matching, time in force, modify and self trade prevention,
  run by CI on GCC, Clang and MSVC.

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

GitHub Actions builds every push and runs the tests on Linux with GCC and with Clang, and on Windows with
MSVC, with warnings turned up on all three. It also checks that the analysis script still runs against the
committed data.

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

That exact run only comes out of MSVC. The C++ standard pins down `std::mt19937_64` exactly, but not the
distributions that turn its output into prices and quantities, and each standard library implements those
its own way. Built with Clang and libc++, the same seed gives a different run from the very first order, so
every figure in this README is the one MSVC produces, and only the Windows build in CI is held to the
committed `simulation.csv`.

Then the plots and statistics:

```
python analysis/analysis.py
```

which needs pandas and matplotlib, and writes into `analysis/plots`.

The informed trader and market maker experiments are a separate program, since they run the simulation
sixty times over and write a good deal more data:

```
./build/experiments
python analysis/experiments.py
```

The first writes into `data/experiments`, which is left out of the repository because it takes about a
minute to regenerate. The second reads it and writes its plots into `analysis/plots`, which are kept.

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

`benchmarks/benchmark.cpp` puts a million orders through the book and measures it two ways. The first
times each kind of operation a million at a time, five times over, and reports the median with the range
beside it. The second times every operation on its own, so that the slow ones show up instead of being
averaged away. A single timing moves around by more than the differences worth measuring, which is worth
knowing before reading anything into one.

Measured on an AMD Ryzen 7 7435HS with MSVC 19.44. Everything quoted below was built with the same flags,
`/MD /O2 /Ob2 /DNDEBUG`, for reasons the section on a measurement mistake explains.

```
submit                    3.51 M ops/s    284.6 ns/op   (279.6 to 296.3)
submit, sized first       5.00 M ops/s    200.0 ns/op   (198.4 to 216.0)
cancel                    7.90 M ops/s    126.5 ns/op   (124.6 to 162.1)
best bid                352.30 M ops/s      2.8 ns/op   (2.8 to 2.9)
bid depth, 5 deep         3.23 M ops/s    309.9 ns/op   (303.7 to 317.5)

                          p50      p90      p99     p99.9         max   (ns)
submit                    220      361      761      4448    32754997
submit, sized first       230      341      691      1232     1575439
submit, second half       230      351      741      3256    32754997
sized, second half        240      351      741      1282     1575439
cancel                    190      210      431       591     1254478
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

### One operation at a time

Timing a single operation needs a finer clock than the standard library offers on Windows, where
`std::chrono::steady_clock` ticks every 100 ns. That is about as long as a cancel takes, so every cancel
would come out as nothing, one tick or two. The per operation figures use the processor's timestamp
counter instead, turned into time using a rate measured against the steady clock over a quarter of a
second. The reads are fenced so the processor cannot move the work being timed outside them, and a pair of
them costs about 30 ns. That is left in the figures rather than subtracted, since taking it off can push the
fastest readings below zero.

The counter is not as fine as its rate makes it sound. It runs at 3.09 ticks a nanosecond, but on this
processor it only moves in steps of 31 ticks, about every 10 ns, so every figure here is to the nearest 10
ns or so. Two figures one step apart are not different in any way worth reading into.

Two things stand out.

The first is that a cancel takes longer at the median here than the throughput figures would suggest, and
30 ns of timer does not account for all of it. What does is that a loop of cancels overlaps them: while one
is waiting on memory, the processor is already fetching for the next. Timing each one on its own forbids
that. A separate run timing the same million cancels three ways in one go shows how much of the gap is
which:

| a million cancels | ns each |
|---|---|
| straight through, as the throughput figures do | 99 |
| with a fence before and after each one, and no timing | 178 |
| with the fences and the counter reads, as the latency figures do | 230 |

So a cancel on its own costs about 180 ns of real work, and the loop hides nearly half of it. Neither
number is wrong. They answer different questions, and an engine handling orders one at a time as they
arrive is asking the second one.

The second is the max. A submit whose median is 220 ns took 33 ms at worst in the run above. A separate run
that printed every submit slower than half a millisecond, along with how many orders were resting at the
time, accounts for every one of the fourteen it found:

- six landed at exactly 22938, 45876, 91751, 183501, 367002 and 734004 resting orders, which are seven
  tenths of a power of two, rounded up. That is the order index reaching its load factor and rehashing
  everything into a table twice the size. Each took almost exactly twice as long as the one before, up to
  31 ms for the last.
- the other eight landed at 40966, 61448, 92171, 138256, 207383, 311074, 466610 and 699914, which are one
  more than the capacities MSVC's `std::vector` grows through at one and a half times a step. That is the
  arena running out of room and copying every order it holds into a bigger one.

Amortised constant time is a promise about the average, and says nothing about the worst single call, which
is the one an order is actually waiting on.

### Sizing the book up front

`OrderBook::reserve` sets aside room in the index, the arena and the free list for a given number of
resting orders before anything is submitted, so a book that stays within that size never has to grow. The
benchmark sends the same orders into a book sized for all of them, and times the two books alternately,
growing, sized, sized, growing, so that neither always runs first:

| | growing | sized first |
|---|---|---|
| submit, a million at a time | 284.6 ns | 200.0 ns |
| p50, one at a time | 220 ns | 230 ns |
| p90 | 361 ns | 341 ns |
| p99 | 761 ns | 691 ns |
| p99.9 | 4448 ns | 1232 ns |
| max | 33 ms | 1.6 ms |

Growing had been costing nearly a third of the average submit, which is more than I expected from something
that happens fourteen times in a million. The tail is where it shows most: the slowest submit in a thousand
drops from 4.4 µs to 1.2 µs, and the slowest of all from 33 ms to 1.6 ms. What is left of the max is the
operating system rather than the book, since cancel, which never grows anything, has one of the same size.

The first version of `reserve` only reserved the memory, and it did nothing for the one in a thousand
figure: 4.6 µs, no better than letting the book grow. Reserving memory only promises it. The operating
system still hands each page over the first time it is written, and those first writes were landing in the
middle of submits. Writing to every page inside `reserve`, while nothing is waiting, is what brought it down
to 1.2 µs.

The median is the one figure that does not improve, and the sized book is a step of the counter behind. A
separate run that split each pass in half put the whole gap in the first half, 200 ns against 240, with none
left in the second. That is most likely because the growing book spends the early part of a run small
enough to stay in cache, and a book sized for a million orders never is. The second half rows in the
benchmark show the two level, within a step, once both are full size.

The simulator does not size its book. It has no way of knowing how big the book will get, and it never
measures latency anyway.

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

The same thing happened again with latency. The first comparison of the growing and sized books timed each
once, one straight after the other, and put the sized book well behind at the 99th percentile, 1323 ns
against 842. Timing them alternately put it slightly ahead instead, 642 against 702, with exactly the same
code. That is why the benchmark now alternates them. Anything measured under different conditions is being
compared with the conditions as much as with anything else.

### What is left

About 285 ns for a submit into a book that grows, and 200 ns into one sized up front, is still not fast. The
index is not free even when it is sized: with three quarters of a million orders resting it is tens of
megabytes, so an insert lands on a cache line nothing has touched recently and pays for the miss however the
table is laid out.

Sizing up front also needs the size to be known. A book that cannot know it would want the index to rehash
a little at a time, moving a few entries across on every insert rather than all of them on one, which keeps
the table small while the book is small and never stalls for long. The other standard step is to lean on
prices being bounded and swap the level map for a flat array indexed by tick. Neither is done, and on the
evidence above I would measure either one before assuming it helps.

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

## Informed traders and a market maker

In the original model nobody knows more than anybody else: every trader prices off the true value as it
stands right now. `src/experiments_main.cpp` adds two kinds of trader on top of that and runs a set of
experiments with them. Each is repeated over ten seeds, because one run of a market maker is one draw of
its P&L and cannot say much on its own, and they are built in pairs that differ in exactly one thing, so
that any difference between the two can be put down to that one thing.

The ordinary traders are given a view of the value that is 300 events out of date. The price still moves,
because their view moves, but it lags, and the lag is something to trade on.

**Informed traders** can see the value as it is now. When the book has drifted more than half a tick away
from it they send an immediate or cancel order for whatever is mispriced, and otherwise they do nothing.

**The market maker** quotes at the touch, rounding its bid down and its offer up so the two sit the same
distance either side of the mid. It keeps each quote a tick short of the other side so that it only ever
rests, stops quoting a side once its position would pass 200, and leans both quotes against its position:
a maker that is long lowers its bid and its offer, so it buys less and sells more and drifts back toward
flat. A second version of it centres its quotes on the view the ordinary traders price from instead of on
the mid, and is only there to test an explanation further down.

### Two versions that did not work

The first version anchored the ordinary traders to the mid instead of to a lagged value. That left the
book anchored to nothing but itself, and the price did not move once in 100,000 events, because orders
placed at the mid refilled the touch faster than anything took from it. The imbalance analysis below still
found a correlation of -0.55 in it, with a t statistic of -11, conjured out of the handful of price moves
among 60,000 rows. That is why the analysis now counts the moves in every sample and will not report a
correlation from fewer than thirty.

The first market maker took its quotes down and put them straight back up every event. Each time, that
sent it to the back of the queue behind every order that had arrived at its price in the meantime, and it
was filled a handful of times in 100,000 events. Leaving a quote alone when the price it wants has not
changed took it to around 2,400 fills a run.

### Does the imbalance in the book predict the next move?

The imbalance is the bid quantity less the ask quantity over the top five levels, divided by the two
together. The table is its correlation with the move in the mid over the orders that follow, taken over
stretches that do not overlap, so the t statistics are not inflated by neighbouring samples sharing most
of their path:

| | next order | 10 orders | 50 orders | 200 orders |
|---|---|---|---|---|
| original model | -0.000 | -0.023 | -0.086 | -0.100 |
| ordinary traders on a 300 event lag | -0.001 | -0.038 | -0.096 | -0.147 |
| plus informed traders | 0.009 | -0.011 | -0.045 | 0.008 |
| plus informed traders looking five times as often | 0.023 | 0.038 | 0.086 | 0.085 |

I expected the textbook answer: that a book heavy on the bid side comes before a rise, and that informed
traders would be what put it there. In the main informed run there is essentially none of that, and only
the next order figure clears a t of 2, just. The explanation I had was that they are too small a part of
the flow to show up in the imbalance at all, since they look at the book on one event in ten but find
something to trade on only about one in eighty. That could be tested, so a run with them looking five
times as often was added, which works out at a little over twice as many trades, because the more they
trade the less often the book is wrong. There the relationship turns positive at every horizon, with t
statistics of 5.7, 3.0 and 3.1 at one, ten and fifty orders. So informed flow does make the imbalance
predictive, and how predictive depends on how much of it there is.

The effects are small all the same. A correlation of 0.086 means the imbalance accounts for less than one
percent of how far the price moves over the next fifty orders. `analysis/plots/imbalance_signal.png` shows
the same thing split into ten groups by imbalance.

### Why it points the wrong way with nobody informed

The negative relationship in the two runs with nobody informed, around -0.1 with t near -3, was the one
result here I could not account for at first. The explanation I tried was stale orders. When the value
moves, the orders already resting on the side it moved away from are left behind, priced for a value that
is no longer there. That side is the heavy one, and the price then moves into it as new orders arrive at
the new value and trade against what was left behind. So a book heavy on the bid side comes before a fall,
the opposite of the textbook, but only because of how that side came to be heavy.

That makes three predictions: the imbalance should line up with how far the mid is from the value, the mid
should move back toward the value, and once that gap is held fixed the imbalance should have nothing left
to say. The experiments now keep the value at every event, so all three can be checked, over fifty orders:

| | imbalance with gap | gap with next move | imbalance with next move | same, gap held fixed |
|---|---|---|---|---|
| original model | 0.303 | -0.541 | -0.086 (t -3.0) | 0.098 (t 3.4) |
| ordinary traders on a 300 event lag | 0.339 | -0.535 | -0.107 (t -3.7) | 0.094 (t 3.3) |
| plus informed traders | 0.092 | -0.537 | -0.010 (t -0.4) | 0.047 (t 1.6) |
| plus informed traders looking five times as often | -0.025 | -0.502 | 0.058 (t 2.0) | 0.052 (t 1.9) |

The first two came out as predicted, and strongly. Where the mid sits above the value the book is heavy on
the bid side, and the mid moves back toward the value over the next fifty orders, with a correlation of
-0.5 in every run.

The third did not. Holding the gap fixed does not leave nothing. It leaves a positive relationship, 0.098
and 0.094 with t statistics of 3.4 and 3.3, which is the textbook one: a book heavy on the bid side does
come before a rise. It was there all along, underneath a larger effect pulling the other way, and the
negative number in the first table is the two of them added together.

With informed traders in the market the gap hardly lines up with the imbalance at all, because they trade
the book back toward the value before it can drift far, and what is left once the gap is held fixed is
smaller and not clearly more than chance. The imbalance with next move column differs a little from the
first table for the lagged runs, because their first 300 events have no lagged value to compare with and
are left out here.

### The market maker

| | fills | P&L | spread earned | lost on position | position rms |
|---|---|---|---|---|---|
| no informed traders | 2448 ± 379 | 1790 ± 351 | 8291 ± 1581 | -6501 ± 1319 | 5.0 ± 0.1 |
| no informed traders, maker sees their view | 723 ± 187 | 2830 ± 734 | 2418 ± 691 | 412 ± 138 | 13.5 ± 1.8 |
| facing informed traders | 2172 ± 266 | 1411 ± 196 | 7214 ± 858 | -5802 ± 718 | 4.9 ± 0.1 |
| facing informed traders, maker sees their view | 534 ± 97 | 1891 ± 402 | 1764 ± 300 | 127 ± 161 | 12.5 ± 1.3 |
| facing informed traders, not leaning | 7548 ± 1243 | 6137 ± 1778 | 21392 ± 3829 | -15255 ± 2748 | 113.3 ± 6.7 |

Money is in ticks times quantity, as the mean and standard deviation over ten seeds. Spread earned is what
the maker made against the mid at the moment of each fill, which is what it would have kept had the price
never moved afterwards, and lost on position is everything else.

It makes money, and did on every one of the ten seeds in all five runs. But the maker quoting around the
mid hands most of its spread back: even with nobody informed in the market it loses 78% of what it earns to
the price moving against it after it trades. The markouts show the same thing from the other side. Taken
over all ten seeds, the maker quoting around the mid earns 0.53 ticks a unit at the moment of the fill and
has 0.12 of it left a thousand events later:

| markout per unit, in ticks | at the fill | 1 event on | 10 | 100 | 1000 |
|---|---|---|---|---|---|
| no informed traders | 0.527 | 0.275 | 0.185 | 0.127 | 0.116 |
| no informed traders, maker sees their view | 0.556 | 0.445 | 0.468 | 0.540 | 0.632 |
| facing informed traders | 0.518 | 0.259 | 0.173 | 0.108 | 0.096 |
| facing informed traders, maker sees their view | 0.554 | 0.439 | 0.454 | 0.499 | 0.576 |

My reading of that was that the maker is the least informed trader in this market. It prices off the mid,
and the mid is made of orders placed from a view of the value that is already 300 events old, so the
maker's picture lags even the ordinary traders', who each price off that view directly. The test was to
give the maker the same view the ordinary traders have and see whether its markouts stop decaying, and the
two runs where the maker sees their view are that test.

The markouts do more than stop decaying. With the view they rise after the fill instead of falling, from
0.56 at the fill to 0.63 a thousand events on, and the maker makes money on its position rather than losing
it, 412 instead of -6501. The change in the markout a thousand events on is 0.515 ± 0.011, more than forty
standard errors, so the explanation holds. It is filled far less often, 723 times against 2448, because
quoting around the view rather than the book puts it off the touch more of the time, but markouts are per
unit filled, so that does not account for the difference, and it still makes more money in total.

Informed traders make things worse for the maker either way. Quoting around the mid, the markout a thousand
events on falls from 0.116 to 0.096, a difference of 0.020 ± 0.006 or 3.3 standard errors, and P&L falls
from 1790 to 1411. Quoting around their view, the markout falls by 0.056 ± 0.019, 2.9 standard errors, and
P&L from 2830 to 1891. The cost is nearly three times as large there, which makes sense: once the maker is
no longer the least informed trader, the informed traders are the only ones left who know more than it
does.

Leaning on position is the other clear result. Without it the maker's position swings out to its
limit and back again, as `analysis/plots/maker_position.png` shows, and with it the typical position is 4.9
instead of 113. The maker that does not lean makes more money, 6137 against 1411, because it stays on both
sides of the touch and is filled three and a half times as often. But its P&L varies far more from one seed
to the next: mean over standard deviation is 7.2 for the maker that leans and 3.5 for the one that does
not. Leaning gives up about three quarters of the return, and gets roughly twice the reliability for it.

## Layout

```
include/        order, trade, price, order index and order book headers
src/            the matching engine, the simulator and the experiments
data/           the csv writer, the simulation output, and the experiment output once it has been run
tests/          the two test suites and the small test helper they share
benchmarks/     the throughput and latency benchmark
analysis/       the pandas scripts and the plots they write
.github/        the workflow that builds and tests every push
```

## What is not in here

Nothing is threaded, and the book is not safe to touch from more than one thread. There is no wire
protocol and no persistence, so the book exists only for as long as the process does.

The main simulation has no informed flow and no market making. Those only exist in the experiments
above, where the market maker is the only trader that holds a position and the informed traders are the
only ones who know anything, and none of the traders learn from what they see. The main simulation only
ever sends good till cancelled limit orders and market orders, the informed traders in the experiments
send immediate or cancel ones, and fill or kill and modify are exercised only by the tests. Self trade
prevention applies to every match: it fired 39 times over the run above, which is about one match in a
thousand, as you would expect from a thousand traders picking orders at random.
