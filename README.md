# msmoney

A small Microsoft Money-style personal finance manager written in C++17 with
SDL3 and Dear ImGui (vendored in `src/vendor/imgui`, using the SDL3 +
SDL_Renderer backends). Text uses the system DejaVu fonts when available,
falling back to ImGui's built-in font.

## Features

- **Global Position** — net worth at a glance: cash, bank accounts, deposits
  and investments with subtotals, plus an asset-allocation bar.
- **Movements** — per-account transaction register with running balance,
  add movements (income/expense), create new accounts (Cash / Bank / Deposit).
- **Investments** — stocks and funds with units, average buy price, current
  price, market value and gain. Buy/Sell (linked to a cash account movement)
  and manual price updates.
- Data is saved automatically to `msmoney.dat` (plain text) in the working
  directory. Delete the file to start over with sample data.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/msmoney
```

## Controls

- `1` / `2` / `3` or the top tabs: switch view
- Click an account or asset row to select it
- Mouse wheel: scroll the movement list
- In dialogs: `Tab` next field, `Enter` accept, `Esc` cancel.
  Decimal comma or point are both accepted in numbers.

## File format

`msmoney.dat` is line-based, `|`-separated:

```
ACCOUNT|id|name|type|initial      (type: 0 cash, 1 bank, 2 deposit)
TX|accountId|date|description|amount
ASSET|id|name|type|units|avgPrice|price   (type: 0 stock, 1 fund)
```
