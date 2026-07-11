// Unit tests for the pure model layer (model.h/model.cpp). No SDL, no ImGui:
// build with just these two files and run; exit code 0 means all checks pass.
#include <cmath>
#include <cstdio>
#include <string>
#include "model.h"

static int failed = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failed++;
    }
}

static void checkNear(double got, double want, const char* what) {
    if (std::fabs(got - want) > 1e-6) {
        printf("FAIL: %s (got %.6f, want %.6f)\n", what, got, want);
        failed++;
    }
}

static void testDates() {
    check(daysBetween("2026-01-01", "2026-01-31") == 30, "january spans 30 days");
    check(daysBetween("2024-02-28", "2024-03-01") == 2, "2024 is a leap year");
    check(daysBetween("2026-07-11", "2026-07-01") == -10, "negative when to < from");
    check(daysBetween("garbage", "2026-01-01") == 0, "unparseable date -> 0");
}

static void testBalanceAndAccrual() {
    Account a{1, "Checking", AccountType::Bank, 100.0};
    a.txs = {{"2026-01-01", "in", 50}, {"2026-01-02", "out", -30}};
    checkNear(a.balance(), 120.0, "balance = initial + movements");
    check(a.accrued() == 0, "non-deposit never accrues");

    Account d{2, "Deposit", AccountType::Deposit, 10000.0, 3.65, "2026-01-01"};
    long days = daysBetween(d.since, todayStr());
    check(days > 0, "test assumes 'today' is after 2026-01-01");
    // 3.65% APR on 10,000 accrues exactly 1.00 per day under ACT/365
    checkNear(d.accrued(), (double)days, "simple interest, ACT/365");
    d.since = todayStr();
    check(d.accrued() == 0, "accrual starts the day after 'since'");
    d.since = "";
    check(d.accrued() == 0, "no accrual without a start date");
}

static void testTrades() {
    Asset s;
    s.type = AssetType::Stock;
    s.price = 12.0;
    s.txs = {{"2026-01-01", 100, 10.0}, {"2026-02-01", 50, 13.0}};
    s.recompute();
    checkNear(s.units, 150, "buys accumulate units");
    checkNear(s.avgPrice, 11.0, "average cost of two buys");
    check(s.realizedGain() == 0, "no sells -> no realized P/L");
    checkNear(s.value(), 150 * 12.0, "value = units * price");

    s.txs.push_back({"2026-03-01", -50, 14.0});
    s.recompute();
    checkNear(s.units, 100, "a sell reduces units");
    checkNear(s.avgPrice, 11.0, "average cost unchanged by a sell");
    checkNear(s.realizedGain(), 50 * (14.0 - 11.0), "realized = sold * (price - avg)");
    checkNear(s.gain(), 100 * (12.0 - 11.0), "unrealized gain on the remainder");

    s.txs.push_back({"2026-04-01", -500, 14.0});  // oversell
    s.recompute();
    checkNear(s.units, 0, "an oversell clamps at zero units");
    checkNear(s.realizedGain(), 50 * 3.0 + 100 * 3.0, "oversell realizes only what was held");
}

static void testPortfolioAndSnapshots() {
    Portfolio pf;
    pf.accounts = {
        {1, "Wallet", AccountType::Cash, 50.0},
        {2, "Bank", AccountType::Bank, 1000.0},
        {3, "Depo", AccountType::Deposit, 2000.0, 3.65, "2026-01-01"},
    };
    pf.assets = {{10, "S", AssetType::Stock, 10, 5, 7},
                 {11, "F", AssetType::Fund, 2, 100, 110}};
    checkNear(pf.totalByType(AccountType::Cash), 50, "cash total");
    checkNear(pf.totalByType(AccountType::Bank), 1000, "bank total");
    checkNear(pf.investmentsValue(), 10 * 7 + 2 * 110, "investments value");
    checkNear(pf.netWorth(),
              50 + 1000 + 2000 + pf.accruedInterest() + pf.investmentsValue(),
              "net worth = accounts + accrued interest + investments");

    Snapshot s = pf.makeSnapshot();
    check(s.date == todayStr(), "snapshot is dated today");
    checkNear(s.stocks, 70, "snapshot splits out stocks");
    checkNear(s.funds, 220, "snapshot splits out funds");
    checkNear(s.deposits, 2000 + pf.accruedInterest(), "snapshot deposits incl. accrued");
    checkNear(s.total(), pf.netWorth(), "snapshot total equals net worth");

    pf.snapshots.push_back({"2020-01-01", 1, 2, 3, 4, 5});
    pf.takeSnapshot();
    pf.takeSnapshot();  // same day again: replaced, not duplicated
    check(pf.snapshots.size() == 2, "at most one snapshot per day");
    check(pf.snapshots.front().date == "2020-01-01", "snapshots stay sorted by date");
}

static void testFormatting() {
    check(fmtMoney(1234.56) == "1,234.56", "thousands separator");
    check(fmtMoney(-987) == "-987.00", "negative sign and two decimals");
    check(fmtMoney(1234567.891) == "1,234,567.89", "millions");
    check(fmtMoney(-0.001) == "0.00", "rounding-noise negatives show no sign");
    check(fmtNum(3.14159, 2) == "3.14", "fmtNum truncates to given decimals");
}

static void testSaveLoadRoundTrip() {
    Portfolio pf;
    Account acc{1, "Cuenta|rara", AccountType::Bank, 100.0};  // '|' must be escaped
    acc.txs = {{"2026-01-05", "Salary", 1234.56}};
    pf.accounts = {acc};
    Asset as{2, "Stock A", AssetType::Stock, 0, 0, 12.5,
             {{"2026-01-02", 10, 10.0}, {"2026-02-02", -4, 11.0}}};
    as.recompute();
    pf.assets = {as};
    pf.snapshots = {{"2026-03-01", 1.11, 2.22, 3.33, 4.44, 5.55}};
    pf.nextId = 3;

    std::string path = "model_tests_tmp.dat";
    check(pf.save(path), "save succeeds");
    Portfolio in;
    check(in.load(path), "load succeeds");
    remove(path.c_str());

    check(in.accounts.size() == 1 && in.assets.size() == 1 && in.snapshots.size() == 1,
          "record counts survive the round trip");
    check(in.accounts[0].name == "Cuenta/rara", "'|' in names is replaced on save");
    checkNear(in.accounts[0].balance(), 1334.56, "movements survive");
    checkNear(in.assets[0].units, 6, "units are recomputed from the trade history");
    checkNear(in.assets[0].realizedGain(), 4 * (11.0 - 10.0), "trade history survives");
    checkNear(in.snapshots[0].total(), 1.11 + 2.22 + 3.33 + 4.44 + 5.55,
              "snapshots survive");
    check(in.nextId == 3, "nextId is rebuilt as highest id + 1");
}

int main() {
    testDates();
    testBalanceAndAccrual();
    testTrades();
    testPortfolioAndSnapshots();
    testFormatting();
    testSaveLoadRoundTrip();
    if (failed) {
        printf("%d check(s) FAILED\n", failed);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
