#include "model.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

double Account::balance() const {
    double b = initial;
    for (const auto& t : txs) b += t.amount;
    return b;
}

// days between two YYYY-MM-DD dates (to - from), negative if to < from
static long daysBetween(const std::string& from, const std::string& to) {
    auto parse = [](const std::string& s, tm& out) {
        int y, m, d;
        if (sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
        out = tm{};
        out.tm_year = y - 1900;
        out.tm_mon = m - 1;
        out.tm_mday = d;
        out.tm_hour = 12;  // midday avoids DST edge cases
        return true;
    };
    tm a{}, b{};
    if (!parse(from, a) || !parse(to, b)) return 0;
    time_t ta = mktime(&a), tb = mktime(&b);
    if (ta == (time_t)-1 || tb == (time_t)-1) return 0;
    return (long)((tb - ta) / 86400);
}

double Account::accrued() const {
    if (type != AccountType::Deposit || rate <= 0 || since.empty()) return 0;
    long days = daysBetween(since, todayStr());
    if (days <= 0) return 0;
    // simple interest, ACT/365, on the current deposit balance
    return balance() * rate / 100.0 * (double)days / 365.0;
}

// walk the trade history chronologically with the average-cost method
static void walkTrades(const std::vector<AssetTx>& txs, double& units, double& avg,
                       double& realized) {
    units = 0;
    avg = 0;
    realized = 0;
    for (const auto& t : txs) {
        if (t.units > 0) {
            avg = (units * avg + t.units * t.price) / (units + t.units);
            units += t.units;
        } else {
            double sold = std::min(-t.units, units);
            realized += sold * (t.price - avg);
            units -= sold;
            if (units < 1e-9) units = 0;  // avg stays for reporting
        }
    }
}

void Asset::recompute() {
    if (txs.empty()) return;  // legacy asset without history: keep stored figures
    double u, avg, realized;
    walkTrades(txs, u, avg, realized);
    units = u;
    avgPrice = avg;
}

double Asset::realizedGain() const {
    double u, avg, realized;
    walkTrades(txs, u, avg, realized);
    return realized;
}

Account* Portfolio::findAccount(const std::string& name) {
    for (auto& a : accounts)
        if (a.name == name) return &a;
    return nullptr;
}

Asset* Portfolio::findAsset(const std::string& name) {
    for (auto& a : assets)
        if (a.name == name) return &a;
    return nullptr;
}

double Portfolio::totalByType(AccountType t) const {
    double s = 0;
    for (const auto& a : accounts)
        if (a.type == t) s += a.balance();
    return s;
}

double Portfolio::accruedInterest() const {
    double s = 0;
    for (const auto& a : accounts) s += a.accrued();
    return s;
}

double Portfolio::investmentsValue() const {
    double s = 0;
    for (const auto& a : assets) s += a.value();
    return s;
}

double Portfolio::investmentsCost() const {
    double s = 0;
    for (const auto& a : assets) s += a.cost();
    return s;
}

double Portfolio::netWorth() const {
    return totalByType(AccountType::Cash) + totalByType(AccountType::Bank) +
           totalByType(AccountType::Deposit) + accruedInterest() + investmentsValue();
}

std::string todayStr() {
    time_t t = time(nullptr);
    tm lt{};
    localtime_r(&t, &lt);
    char buf[16];
    strftime(buf, sizeof buf, "%Y-%m-%d", &lt);
    return buf;
}

std::string fmtMoney(double v) {
    bool neg = v < -0.005;
    char buf[64];
    snprintf(buf, sizeof buf, "%.2f", std::fabs(v));
    std::string s(buf);
    size_t dot = s.find('.');
    for (int i = (int)dot - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return (neg ? "-" : "") + s;
}

std::string fmtNum(double v, int dec) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.*f", dec, v);
    return buf;
}

// ---- persistence: simple '|'-separated line format ----

static std::string clean(std::string s) {
    for (auto& c : s)
        if (c == '|' || c == '\n') c = '/';
    return s;
}

static std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string part;
    while (std::getline(ss, part, '|')) out.push_back(part);
    return out;
}

bool Portfolio::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "# msmoney data file\n";
    for (const auto& a : accounts) {
        f << "ACCOUNT|" << a.id << '|' << clean(a.name) << '|' << (int)a.type
          << '|' << fmtNum(a.initial, 2) << '|' << fmtNum(a.rate, 3) << '|' << a.since
          << '\n';
        for (const auto& t : a.txs)
            f << "TX|" << a.id << '|' << t.date << '|' << clean(t.desc)
              << '|' << fmtNum(t.amount, 2) << '\n';
    }
    for (const auto& s : assets) {
        f << "ASSET|" << s.id << '|' << clean(s.name) << '|' << (int)s.type
          << '|' << fmtNum(s.units, 4) << '|' << fmtNum(s.avgPrice, 4)
          << '|' << fmtNum(s.price, 4) << '\n';
        for (const auto& t : s.txs)
            f << "ATX|" << s.id << '|' << t.date << '|' << fmtNum(t.units, 4) << '|'
              << fmtNum(t.price, 4) << '\n';
    }
    return true;
}

bool Portfolio::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    accounts.clear();
    assets.clear();
    nextId = 1;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto p = split(line);
        if (p[0] == "ACCOUNT" && p.size() >= 5) {
            Account a;
            a.id = atoi(p[1].c_str());
            a.name = p[2];
            a.type = (AccountType)atoi(p[3].c_str());
            a.initial = atof(p[4].c_str());
            if (p.size() > 5) a.rate = atof(p[5].c_str());
            if (p.size() > 6) a.since = p[6];
            accounts.push_back(a);
            nextId = std::max(nextId, a.id + 1);
        } else if (p[0] == "TX" && p.size() >= 5) {
            int id = atoi(p[1].c_str());
            for (auto& a : accounts)
                if (a.id == id)
                    a.txs.push_back({p[2], p[3], atof(p[4].c_str())});
        } else if (p[0] == "ASSET" && p.size() >= 7) {
            Asset s;
            s.id = atoi(p[1].c_str());
            s.name = p[2];
            s.type = (AssetType)atoi(p[3].c_str());
            s.units = atof(p[4].c_str());
            s.avgPrice = atof(p[5].c_str());
            s.price = atof(p[6].c_str());
            assets.push_back(s);
            nextId = std::max(nextId, s.id + 1);
        } else if (p[0] == "ATX" && p.size() >= 5) {
            int id = atoi(p[1].c_str());
            for (auto& s : assets)
                if (s.id == id)
                    s.txs.push_back({p[2], atof(p[3].c_str()), atof(p[4].c_str())});
        }
    }
    // files from before trade history: turn the stored position into an
    // opening trade so gains can be computed from the history
    for (auto& s : assets) {
        if (s.txs.empty() && s.units > 0)
            s.txs.push_back({"", s.units, s.avgPrice});
        s.recompute();
    }
    return true;
}

void Portfolio::seed() {
    Account wallet{nextId++, "Wallet", AccountType::Cash, 180.0, {}};
    wallet.txs = {
        {"2026-06-28", "Coffee", -3.20},
        {"2026-07-02", "Taxi", -14.50},
        {"2026-07-05", "Market stall", -22.75},
    };
    Account checking{nextId++, "Main Checking", AccountType::Bank, 2350.0, {}};
    checking.txs = {
        {"2026-06-25", "Salary June", 2100.00},
        {"2026-06-27", "Rent", -850.00},
        {"2026-06-30", "Groceries", -126.40},
        {"2026-07-01", "Electricity bill", -64.90},
        {"2026-07-03", "Restaurant", -48.00},
        {"2026-07-06", "Internet", -35.00},
    };
    Account savings{nextId++, "Savings Account", AccountType::Bank, 5200.0, {}};
    savings.txs = {
        {"2026-07-01", "Monthly saving", 300.00},
    };
    Account deposit{nextId++, "Fixed Deposit 12m", AccountType::Deposit, 10000.0, 3.1,
                    "2026-01-01", {}};
    accounts = {wallet, checking, savings, deposit};

    assets = {
        {nextId++, "Telefonica", AssetType::Stock, 0, 0, 4.36,
         {{"2026-02-10", 150, 3.80}, {"2026-04-22", 50, 4.20}}},
        {nextId++, "Apple", AssetType::Stock, 0, 0, 192.30,
         {{"2026-03-05", 14, 175.00}, {"2026-06-18", -4, 188.10}}},
        {nextId++, "iShares MSCI World", AssetType::Fund, 0, 0, 91.40,
         {{"2026-01-15", 15.5, 82.10}}},
        {nextId++, "Vanguard Global Bond", AssetType::Fund, 0, 0, 24.85,
         {{"2026-04-15", 40, 25.30}}},
    };
    for (auto& s : assets) s.recompute();
}
