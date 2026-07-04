# IRS Engine

Modern C++20 interest rate swap and yield curve engine for fixed-income pricing, bootstrapping, and basic risk analytics.

License: MIT. This project was created for educational and reproducible-demo purposes.

## What it does

- Bootstraps a continuously compounded yield curve from market quotes.
- Interpolates zero rates and discount factors across arbitrary maturities.
- Prices fixed and floating legs of a vanilla interest rate swap.
- Computes swap NPV, fair par rate, and DV01 using a parallel 1 bp curve shift.
- Ingests market inputs from CSV files or runs with the built-in public India curve snapshot.

## Project layout

- `include/YieldCurve.h` - curve, date, cash flow, leg, and swap interfaces.
- `src/YieldCurve.cpp` - curve bootstrapping, interpolation, and swap math.
- `src/main.cpp` - CSV ingestion, demo wiring, and output formatting.
- `data/market_quotes.csv` - sample market curve input.
- `data/trade.csv` - sample swap trade input.
- `Makefile` - build rules for the executable.

## Requirements

- C++20 compiler
- `make`

The project only uses the C++ standard library.

## Build

From the project root:

```bash
make -C /home/arachnid/irs_engine
```

The executable is written to `bin/pricer`.

## Run

Run with the included CSV files:

```bash
./bin/pricer data/market_quotes.csv data/trade.csv
```

Run with no arguments to use the built-in market snapshot and trade defaults:

```bash
./bin/pricer
```

## CSV formats

### Market quotes CSV

Header:

```text
type,maturity,rate,year_fraction,payment_interval
```

Supported rows:

- `deposit` for short-dated instruments.
- `swap` for longer-dated swap pillars.

Example:

```text
deposit,1.0,0.05715,1.0,
swap,2.0,0.05995,,1.0
swap,3.0,0.06212,,1.0
swap,5.0,0.06425,,1.0
swap,10.0,0.06707,,1.0
```

### Trade CSV

Header:

```text
notional,fixed_rate,maturity,payment_interval,side,spread,start_time
```

Example:

```text
100000000,0.0400,5.0,1.0,PayFixed,0.0,0.0
```

## Data provenance

The bundled market snapshot is not synthetic. It uses a public India government bond curve snapshot visible on 4 Jul 2026 from World Government Bonds, with the following pillars used in the sample input:

- 1Y: 5.715%
- 2Y: 5.995%
- 3Y: 6.212%
- 5Y: 6.425%
- 10Y: 6.707%

This is a practical public proxy for India market data and is suitable for reproducible demos.

## Sample output

Example output from the CSV-driven run:

```text
Market inputs
  Source:   Public India government bond curve snapshot
  As of:    4 Jul 2026
  Deposits: 1
  Swaps:    4
  Trade notional:     1e+08
  Trade fixed coupon: 0.04
  Trade maturity:     5
  Trade interval:     1

Bootstrapped pillars
  t = 1.000000  zero = 0.055577  df = 0.945940
  t = 2.000000  zero = 0.058301  df = 0.889939
  t = 3.000000  zero = 0.060452  df = 0.834139
  t = 5.000000  zero = 0.062604  df = 0.731237
  t = 10.000000  zero = 0.065686  df = 0.518477

Swap valuation summary
  Notional:      100000000.0000
  Fixed coupon:  0.0400
  Fair par rate: 0.0642
  NPV:           10143990.8158
  DV01:          41366.5311
```

The full program output also prints an interpolated curve grid and detailed fixed/floating cash flow tables.

## Notes

- The curve uses linear interpolation of continuous zero rates.
- The bootstrap is calibrated from the input pillars and then extended through the swap maturities.
- The fixed and floating legs currently use annual payments in the sample, which keeps the example concise and easy to verify.

## Cleaning build artifacts

```bash
make -C /home/arachnid/irs_engine clean
```
