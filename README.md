# msmoney

A small Microsoft Money-style personal finance manager written in C++17 with
SDL3 and Dear ImGui (vendored in `src/vendor/imgui`, using the SDL3 +
SDL_Renderer backends). Text uses the system DejaVu fonts when available,
falling back to ImGui's built-in font.

## Features

- **Global Position** — net worth at a glance: cash, bank accounts, deposits
  and investments with subtotals, plus an asset-allocation bar. Deposits show
  their interest rate and an "Accrued interest (unpaid)" line computed daily
  (simple interest, ACT/365) that counts toward the subtotal and net worth.
- **Movements** — per-account transaction register with running balance,
  add movements (income/expense), create new accounts (Cash / Bank / Deposit).
  Deposits have no movement register: selecting one shows a detail card with
  principal, rate, accrual start and accrued interest, plus an *Edit terms*
  button. When the bank actually pays the interest, record it as a movement
  on a bank account and move the deposit's accrual date forward.
- **Investments** — stocks and funds with units, average buy price, current
  price (NAV for funds), market value and gain. Buy/Sell (linked to a cash
  account movement) and manual price/NAV updates.
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
ACCOUNT|id|name|type|initial|rate|since   (type: 0 cash, 1 bank, 2 deposit;
                                           rate/since: deposit interest terms)
TX|accountId|date|description|amount
ASSET|id|name|type|units|avgPrice|price   (type: 0 stock, 1 fund)
```
