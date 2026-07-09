#pragma once
#include <string>
#include <vector>

enum class AccountType { Cash, Bank, Deposit };
enum class AssetType { Stock, Fund };

struct Transaction {
    std::string date;   // YYYY-MM-DD
    std::string desc;
    double amount = 0;  // + income, - expense
};

struct Account {
    int id = 0;
    std::string name;
    AccountType type = AccountType::Bank;
    double initial = 0;
    double rate = 0;        // deposits: annual interest rate in %
    std::string since;      // deposits: accrual start date (YYYY-MM-DD)
    std::vector<Transaction> txs;
    double balance() const;
    double accrued() const;  // accrued unpaid interest (deposits only)
};

struct AssetTx {
    std::string date;   // YYYY-MM-DD
    double units = 0;   // > 0 buy, < 0 sell
    double price = 0;   // execution price per unit
    double amount() const { return units * price; }
};

struct Asset {
    int id = 0;
    std::string name;
    AssetType type = AssetType::Stock;
    double units = 0;      // derived from txs by recompute()
    double avgPrice = 0;   // average purchase price, derived from txs
    double price = 0;      // current market price / NAV
    std::vector<AssetTx> txs;
    void recompute();              // rebuild units/avgPrice from the history
    double realizedGain() const;   // P/L already locked in by sells
    double value() const { return units * price; }
    double cost() const { return units * avgPrice; }
    double gain() const { return value() - cost(); }  // unrealized
    double gainPct() const { return cost() > 0 ? gain() / cost() * 100.0 : 0; }
    double totalGain() const { return gain() + realizedGain(); }
};

struct Portfolio {
    std::vector<Account> accounts;
    std::vector<Asset> assets;
    int nextId = 1;

    Account* findAccount(const std::string& name);
    Asset* findAsset(const std::string& name);
    double totalByType(AccountType t) const;
    double accruedInterest() const;  // sum over all deposits
    double investmentsValue() const;
    double investmentsCost() const;
    double netWorth() const;         // includes accrued unpaid interest

    bool load(const std::string& path);
    bool save(const std::string& path) const;
    void seed();   // sample data for first run
};

std::string todayStr();
std::string fmtMoney(double v);          // 1,234.56  /  -987.00
std::string fmtNum(double v, int dec);   // plain number, no separators
