# OmniMatch — Real-Time Limit Order Book & Market-Making Simulator

A multi-threaded limit order book matching engine written in C++, paired with an
Avellaneda–Stoikov market maker, synthetic noise traders, and a live browser
terminal that streams the book, the trade tape, and the mid-price in real time.

The goal of the project is to make market microstructure *observable*: instead of
reading about how a maker manages inventory or how price discovery emerges from
order flow, you can watch it happen live as the engine runs.



https://github.com/user-attachments/assets/9a2ecf24-5399-40d2-853a-4c3718956381



---

## What it does

- **Matching engine** — price–time priority, with a `std::map<Price, std::list<OrderPtr>>`
  for the bid/ask ladders and an `unordered_map` for O(1) order lookup and cancellation.
- **Order types** — `Market`, `GoodTillCancel`, `FillAndKill`, `FillOrKill`, and
  `GoodForDay` (the last auto-expired by a background prune thread).
- **Avellaneda–Stoikov market maker** — quotes around an inventory-adjusted
  reservation price `r = s − q·γ·σ`, and **skews quote sizes by inventory** so the
  book actively mean-reverts the maker's position back toward flat.
- **Synthetic order flow** — two independent noise-trader agents submit Gaussian-distributed
  orders, against which spread dynamics and price discovery can be studied.
- **Live terminal** — a single self-contained HTML page renders the order book,
  a time-&-sales tape, a mid-price chart, and the maker's inventory / reservation
  price, updating continuously while the engine runs.

---

## Architecture

The engine runs several threads concurrently against one shared order book.
Shared state is coordinated with `std::atomic` for scalar signals (mid price,
maker inventory, volatility) and a `std::scoped_lock` / `std::mutex` guarding all
book mutations.

```
                          ┌─────────────────────────────┐
   Avellaneda–Stoikov ───▶│                             │
   Noise trader  #1   ───▶│   Orderbook (mutex-guarded)  │───▶ snapshot ──┐
   Noise trader  #2   ───▶│   map + list + unordered_map │              │
                          └─────────────────────────────┘              │
                                       ▲                                │
                       GoodForDay prune thread (condition_variable)     │
                                                                        ▼
                                                          Broadcaster thread
                                                          (100 ms snapshots)
                                                                        │
                                                                        ▼
                                                          Browser terminal (web/)
```

| Thread | Role |
|--------|------|
| `AvellanedaStoikovMM` | Re-quotes around the reservation price; skews size by inventory |
| `TraderAgent` (×2) | Emit Gaussian random orders as noise flow |
| `Broadcaster` | Snapshots the book every 100 ms and pushes it to connected clients |
| `PruneGoodForDayOrders` | Background expiry of GoodForDay orders via `condition_variable` |

---

## Project structure

```
.
├── src/
│   ├── Globals.h        # shared types (Price, Quantity, OrderId) + global signals
│   ├── Orderbook.h      # Order and Orderbook class declarations
│   ├── Orderbook.cpp    # matching, cancel, snapshot, level data, prune logic
│   └── main.cpp         # threads, strategies, and the streaming server
├── web/
│   └── index.html       # self-contained live terminal (canvas chart, book, tape)
├── Makefile
└── README.md
```

---

## Build & run

**Requirements:** a C++17 compiler (`g++` or `clang++`), `make`, and a POSIX
environment (Linux or macOS). No external libraries.

```bash
# clone
git clone https://github.com/RaresVictor/OrderBook.git
cd OrderBook

# build
make

# run the engine (starts the simulation + streaming server)
./omnimatch        # adjust to your Makefile's output name
```

Then open `web/index.html` in a browser. The status badge turns **LIVE** once it
connects to the running engine, and the book, tape, and chart begin updating.

> The engine and the page are decoupled — the engine streams snapshots, the page
> just renders whatever it receives, so you can refresh or reconnect freely while
> the simulation keeps running.

---

## Design notes

A few decisions worth calling out:

- **Why `map` + `list` + `unordered_map`.** The ordered `map` keeps price levels
  sorted for best-bid/best-ask access; the `list` at each level preserves time
  priority and gives O(1) erase; the `unordered_map` stores each order's iterator
  so cancellation is O(1) without scanning the book.
- **Inventory skew, not just spread.** A textbook A–S maker centers quotes on the
  reservation price; here the maker also shrinks the quote on the side that would
  worsen its inventory and enlarges the other, so large positions get worked back
  toward flat faster than symmetric quoting would manage.
- **Snapshot, don't stream-per-event.** The book is snapshotted on a fixed cadence
  rather than emitting an event per trade, which keeps the rendering load bounded
  no matter how fast the engine matches.

---

## Possible next steps

- Add throughput/latency benchmarking (orders matched per second, match latency
  percentiles) to quantify engine performance.
- Parameter sweep over `γ` (risk aversion) and noise-trader intensity to chart
  the maker's PnL/inventory trade-off.
- Persist a session to disk for offline replay.

---

*Built as a personal project to study market microstructure and concurrent systems design in C++.*
