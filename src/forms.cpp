// All modal dialogs: prefill (openForm), validation/submit, and drawing.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include "ui.h"

static std::optional<double> parseNum(std::string s) {
    for (auto& c : s)
        if (c == ',') c = '.';
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '%')) s.pop_back();
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    double v = strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return std::nullopt;
    return v;
}

static void sortTxs(Account& acc) {
    std::stable_sort(acc.txs.begin(), acc.txs.end(),
                     [](const Transaction& x, const Transaction& y) { return x.date < y.date; });
}

static void addTrade(Asset& as, const std::string& date, double units, double price) {
    as.txs.push_back({date, units, price});
    std::stable_sort(as.txs.begin(), as.txs.end(),
                     [](const AssetTx& x, const AssetTx& y) { return x.date < y.date; });
    as.recompute();
}

static bool ComboStrings(const char* label, int* idx, const std::vector<std::string>& items) {
    bool changed = false;
    const char* preview = items.empty() ? "" : items[std::clamp(*idx, 0, (int)items.size() - 1)].c_str();
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < (int)items.size(); i++)
            if (ImGui::Selectable(items[i].c_str(), i == *idx)) {
                *idx = i;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

static std::vector<std::string> assetNames(const Portfolio& pf) {
    std::vector<std::string> v;
    for (auto& s : pf.assets) v.push_back(s.name);
    return v;
}

// ---- forms -----------------------------------------------------------------
void openForm(App& a, FormKind k) {
    a.bufs = FormBufs{};
    FormBufs& b = a.bufs;
    Asset* as = (a.selAsset >= 0 && a.selAsset < (int)a.pf.assets.size())
                    ? &a.pf.assets[a.selAsset]
                    : nullptr;
    switch (k) {
        case FormKind::AddMovement:
            snprintf(b.date, sizeof b.date, "%s", todayStr().c_str());
            break;
        case FormKind::NewAccount:
            b.typeIdx = 1;  // Bank
            snprintf(b.initial, sizeof b.initial, "0");
            snprintf(b.date, sizeof b.date, "%s", todayStr().c_str());  // accrual start
            break;
        case FormKind::EditDeposit:
            if (Account* acc = selAccount(a)) {
                snprintf(b.rate, sizeof b.rate, "%s", fmtNum(acc->rate, 2).c_str());
                snprintf(b.date, sizeof b.date, "%s",
                         acc->since.empty() ? todayStr().c_str() : acc->since.c_str());
            }
            break;
        case FormKind::Buy:
            if (as) {
                snprintf(b.name, sizeof b.name, "%s", as->name.c_str());
                b.typeIdx = as->type == AssetType::Fund ? 1 : 0;
            }
            snprintf(b.date, sizeof b.date, "%s", todayStr().c_str());
            break;
        case FormKind::Sell:
        case FormKind::SetPrice:
            b.assetIdx = as ? a.selAsset : 0;
            if (!a.pf.assets.empty()) {
                Asset& t = a.pf.assets[b.assetIdx];
                snprintf(b.price, sizeof b.price, "%s", fmtNum(t.price, 2).c_str());
            }
            snprintf(b.date, sizeof b.date, "%s", todayStr().c_str());
            break;
        default: break;
    }
    a.pending = k;
}

// each submit* returns true on success (popup closes) or sets bufs.error
static bool submitAddMovement(App& a) {
    FormBufs& b = a.bufs;
    Account* acc = selAccount(a);
    if (!acc) { b.error = "No account selected"; return false; }
    if (acc->type == AccountType::Deposit) {
        b.error = "Deposits have no movements; adjust their terms instead";
        return false;
    }
    auto amt = parseNum(b.amount);
    if (!amt || *amt == 0) { b.error = "Amount must be a non-zero number"; return false; }
    std::string date = b.date[0] ? b.date : todayStr();
    acc->txs.push_back({date, b.desc[0] ? b.desc : "Movement", *amt});
    sortTxs(*acc);
    a.scrollEnd = true;
    setStatus(a, "Movement added to " + acc->name);
    return true;
}

static bool submitNewAccount(App& a) {
    FormBufs& b = a.bufs;
    if (!b.name[0]) { b.error = "Name is required"; return false; }
    if (a.pf.findAccount(b.name)) { b.error = "An account with that name exists"; return false; }
    auto ini = parseNum(b.initial);
    AccountType t = b.typeIdx == 0   ? AccountType::Cash
                    : b.typeIdx == 2 ? AccountType::Deposit
                                     : AccountType::Bank;
    Account acc{0, b.name, t, ini ? *ini : 0.0, 0, "", {}};
    if (t == AccountType::Deposit && b.rate[0]) {
        auto r = parseNum(b.rate);
        if (!r || *r < 0) { b.error = "Interest rate must be a number >= 0"; return false; }
        acc.rate = *r;
        acc.since = b.date[0] ? b.date : todayStr();
    }
    acc.id = a.pf.nextId++;
    a.pf.accounts.push_back(acc);
    a.selAcc = (int)a.pf.accounts.size() - 1;
    setStatus(a, std::string("Account created: ") + b.name);
    return true;
}

static bool submitBuy(App& a) {
    FormBufs& b = a.bufs;
    auto units = parseNum(b.units), price = parseNum(b.price);
    if (!b.name[0]) { b.error = "Asset name is required"; return false; }
    if (!units || *units <= 0) { b.error = "Units must be > 0"; return false; }
    if (!price || *price <= 0) { b.error = "Price must be > 0"; return false; }
    std::string date = b.date[0] ? b.date : todayStr();
    Asset* as = a.pf.findAsset(b.name);
    if (!as) {
        // new asset: the market price/NAV starts at the purchase price and is
        // then only changed through the Update price dialog
        a.pf.assets.push_back({a.pf.nextId++, b.name,
                               b.typeIdx == 1 ? AssetType::Fund : AssetType::Stock, 0, 0,
                               *price, {}});
        as = &a.pf.assets.back();
        a.selAsset = (int)a.pf.assets.size() - 1;
    }
    addTrade(*as, date, *units, *price);
    setStatus(a, "Bought " + fmtNum(*units, 2) + " " + b.name + " for " +
                     fmtMoney(*units * *price));
    return true;
}

static bool submitSell(App& a) {
    FormBufs& b = a.bufs;
    if (a.pf.assets.empty()) { b.error = "No assets"; return false; }
    Asset& as = a.pf.assets[std::clamp(b.assetIdx, 0, (int)a.pf.assets.size() - 1)];
    auto units = parseNum(b.units), price = parseNum(b.price);
    if (!units || *units <= 0 || *units > as.units + 1e-9) {
        b.error = "Units must be > 0 and <= " + fmtNum(as.units, 4);
        return false;
    }
    if (!price || *price <= 0) { b.error = "Price must be > 0"; return false; }
    double realized = (*price - as.avgPrice) * *units;
    addTrade(as, b.date[0] ? b.date : todayStr(), -*units, *price);
    setStatus(a, "Sold " + fmtNum(*units, 2) + " " + as.name + ", realized P/L " +
                     fmtMoney(realized));
    return true;
}

static bool submitDeleteMovement(App& a) {
    Account* acc = selAccount(a);
    if (!acc || a.selTx < 0 || a.selTx >= (int)acc->txs.size()) return false;
    setStatus(a, "Deleted movement: " + acc->txs[a.selTx].desc + " (" +
                     fmtMoney(acc->txs[a.selTx].amount) + ")");
    acc->txs.erase(acc->txs.begin() + a.selTx);
    a.selTx = -1;
    return true;
}

static bool submitEditDeposit(App& a) {
    FormBufs& b = a.bufs;
    Account* acc = selAccount(a);
    if (!acc) { b.error = "No account selected"; return false; }
    auto r = parseNum(b.rate);
    if (!r || *r < 0) { b.error = "Rate must be a number >= 0"; return false; }
    acc->rate = *r;
    acc->since = b.date[0] ? b.date : todayStr();
    setStatus(a, acc->name + ": " + fmtNum(*r, 2) + "% APR since " + acc->since);
    return true;
}

static bool submitDeleteAccount(App& a) {
    Account* acc = selAccount(a);
    if (!acc) return false;
    setStatus(a, "Deleted account: " + acc->name);
    a.pf.accounts.erase(a.pf.accounts.begin() + a.selAcc);
    a.selAcc = std::max(0, a.selAcc - 1);
    return true;
}

static bool submitDeleteAsset(App& a) {
    if (a.selAsset < 0 || a.selAsset >= (int)a.pf.assets.size()) return false;
    setStatus(a, "Deleted asset: " + a.pf.assets[a.selAsset].name);
    a.pf.assets.erase(a.pf.assets.begin() + a.selAsset);
    a.selAsset = -1;
    return true;
}

static bool submitDeleteSnapshot(App& a) {
    if (a.selSnap < 0 || a.selSnap >= (int)a.pf.snapshots.size()) return false;
    setStatus(a, "Deleted snapshot " + a.pf.snapshots[a.selSnap].date);
    a.pf.snapshots.erase(a.pf.snapshots.begin() + a.selSnap);
    a.selSnap = -1;
    return true;
}

static bool submitSetPrice(App& a) {
    FormBufs& b = a.bufs;
    if (a.pf.assets.empty()) { b.error = "No assets"; return false; }
    Asset& as = a.pf.assets[std::clamp(b.assetIdx, 0, (int)a.pf.assets.size() - 1)];
    auto price = parseNum(b.price);
    if (!price || *price <= 0) { b.error = "Price must be > 0"; return false; }
    as.price = *price;
    setStatus(a, as.name + " price updated to " + fmtMoney(*price));
    return true;
}

// shared modal footer: error line + OK/Cancel; returns true if OK clicked/Enter
static bool formFooter(App& a) {
    if (!a.bufs.error.empty()) ImGui::TextColored(C_RED, "%s", a.bufs.error.c_str());
    ImGui::Spacing();
    bool ok = AccentButton("OK", C_GREEN, C_DARK, ImVec2(100, 0));
    ImGui::SameLine();
    // Only react to Enter/Escape when the modal itself has focus. While a combo
    // dropdown (a child popup) is open, those keys belong to the dropdown -
    // otherwise picking a value with the keyboard would also submit/close the
    // whole dialog.
    bool focused = ImGui::IsWindowFocused();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) ||
        (focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
        ImGui::CloseCurrentPopup();
    ok |= focused && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    return ok;
}

void drawForms(App& a) {
    FormBufs& b = a.bufs;
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();

    auto beginModal = [&](const char* title) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(440, 0));
        return ImGui::BeginPopupModal(title, nullptr,
                                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    };
    auto finish = [&](bool submitted) {
        if (submitted) {
            a.pf.save(a.path);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    };

    if (beginModal("Add movement")) {
        ImGui::InputText("Date (YYYY-MM-DD)", b.date, sizeof b.date);
        ImGui::InputText("Description", b.desc, sizeof b.desc);
        ImGui::InputText("Amount (+ in, - out)", b.amount, sizeof b.amount);
        finish(formFooter(a) && submitAddMovement(a));
    }
    if (beginModal("New account")) {
        ImGui::InputText("Name", b.name, sizeof b.name);
        static const std::vector<std::string> types = {"Cash", "Bank", "Deposit"};
        ComboStrings("Type", &b.typeIdx, types);
        ImGui::InputText("Initial balance", b.initial, sizeof b.initial);
        if (b.typeIdx == 2) {  // Deposit
            ImGui::InputText("Annual interest %", b.rate, sizeof b.rate);
            ImGui::InputText("Interest since (YYYY-MM-DD)", b.date, sizeof b.date);
        }
        finish(formFooter(a) && submitNewAccount(a));
    }
    if (beginModal("Buy stock / fund")) {
        ImGui::InputText("Asset name", b.name, sizeof b.name);
        static const std::vector<std::string> types = {"Stock", "Fund"};
        ComboStrings("Type", &b.typeIdx, types);
        ImGui::InputText("Date (YYYY-MM-DD)", b.date, sizeof b.date);
        ImGui::InputText("Units", b.units, sizeof b.units);
        ImGui::InputText(b.typeIdx == 1 ? "NAV per unit" : "Price per unit", b.price,
                         sizeof b.price);
        ImGui::TextColored(C_DIM, "Only units and average cost change; update the\n"
                                  "market price/NAV separately.");
        finish(formFooter(a) && submitBuy(a));
    }
    if (beginModal("Delete account")) {
        if (Account* acc = selAccount(a)) {
            ImGui::Text("Delete the account \"%s\"?", acc->name.c_str());
            ImGui::TextColored(C_DIM, "%s - balance %s - %d movements",
                               accTypeName(acc->type), fmtMoney(acc->balance()).c_str(),
                               (int)acc->txs.size());
            ImGui::TextColored(C_RED, "This cannot be undone.");
            ImGui::Spacing();
            bool del = AccentButton("Delete", C_RED, C_TEXT, ImVec2(100, 0));
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0)) ||
                (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                ImGui::CloseCurrentPopup();
            finish(del && submitDeleteAccount(a));
        } else {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (beginModal("Delete asset")) {
        if (a.selAsset >= 0 && a.selAsset < (int)a.pf.assets.size()) {
            Asset& as = a.pf.assets[a.selAsset];
            ImGui::Text("Delete the %s \"%s\"?",
                        as.type == AssetType::Fund ? "fund" : "stock", as.name.c_str());
            ImGui::TextColored(C_DIM, "%s units - market value %s",
                               fmtNum(as.units, 2).c_str(), fmtMoney(as.value()).c_str());
            ImGui::TextColored(C_RED, "This cannot be undone. No cash movement is recorded.");
            ImGui::Spacing();
            bool del = AccentButton("Delete", C_RED, C_TEXT, ImVec2(100, 0));
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0)) ||
                (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                ImGui::CloseCurrentPopup();
            finish(del && submitDeleteAsset(a));
        } else {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (beginModal("Delete movement")) {
        Account* acc = selAccount(a);
        if (acc && a.selTx >= 0 && a.selTx < (int)acc->txs.size()) {
            Transaction& t = acc->txs[a.selTx];
            ImGui::Text("Delete this movement from \"%s\"?", acc->name.c_str());
            ImGui::TextColored(C_DIM, "%s - %s - %s", t.date.c_str(), t.desc.c_str(),
                               fmtMoney(t.amount).c_str());
            ImGui::TextColored(C_RED, "The balance will change by %s. This cannot be undone.",
                               fmtMoney(-t.amount).c_str());
            ImGui::Spacing();
            bool del = AccentButton("Delete", C_RED, C_TEXT, ImVec2(100, 0));
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0)) ||
                (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                ImGui::CloseCurrentPopup();
            finish(del && submitDeleteMovement(a));
        } else {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (beginModal("Delete snapshot")) {
        if (a.selSnap >= 0 && a.selSnap < (int)a.pf.snapshots.size()) {
            Snapshot& s = a.pf.snapshots[a.selSnap];
            ImGui::Text("Delete the snapshot from %s?", s.date.c_str());
            ImGui::TextColored(C_DIM, "Recorded net worth: %s", fmtMoney(s.total()).c_str());
            ImGui::TextColored(C_RED, "This cannot be undone.");
            ImGui::Spacing();
            bool del = AccentButton("Delete", C_RED, C_TEXT, ImVec2(100, 0));
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0)) ||
                (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                ImGui::CloseCurrentPopup();
            finish(del && submitDeleteSnapshot(a));
        } else {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (beginModal("Deposit terms")) {
        if (Account* acc = selAccount(a))
            ImGui::TextColored(C_DIM, "%s - balance %s", acc->name.c_str(),
                               fmtMoney(acc->balance()).c_str());
        ImGui::InputText("Annual interest %", b.rate, sizeof b.rate);
        ImGui::InputText("Accruing since (YYYY-MM-DD)", b.date, sizeof b.date);
        ImGui::TextColored(C_DIM, "Simple interest, ACT/365, on the current balance.");
        finish(formFooter(a) && submitEditDeposit(a));
    }
    // picking another asset refreshes the prefilled price for that asset
    auto refreshPrice = [&] {
        if (a.pf.assets.empty()) return;
        Asset& as = a.pf.assets[std::clamp(b.assetIdx, 0, (int)a.pf.assets.size() - 1)];
        snprintf(b.price, sizeof b.price, "%s", fmtNum(as.price, 2).c_str());
    };
    if (beginModal("Sell stock / fund")) {
        if (ComboStrings("Asset", &b.assetIdx, assetNames(a.pf))) refreshPrice();
        if (!a.pf.assets.empty()) {
            Asset& as = a.pf.assets[std::clamp(b.assetIdx, 0, (int)a.pf.assets.size() - 1)];
            ImGui::TextColored(C_DIM, "Held: %s units, avg buy %s", fmtNum(as.units, 2).c_str(),
                               fmtMoney(as.avgPrice).c_str());
        }
        ImGui::InputText("Date (YYYY-MM-DD)", b.date, sizeof b.date);
        ImGui::InputText("Units", b.units, sizeof b.units);
        ImGui::InputText("Price per unit", b.price, sizeof b.price);
        finish(formFooter(a) && submitSell(a));
    }
    if (beginModal("Update price / NAV")) {
        if (ComboStrings("Asset", &b.assetIdx, assetNames(a.pf))) refreshPrice();
        bool isFund = false;
        if (!a.pf.assets.empty()) {
            Asset& as = a.pf.assets[std::clamp(b.assetIdx, 0, (int)a.pf.assets.size() - 1)];
            isFund = as.type == AssetType::Fund;
            ImGui::TextColored(C_DIM, "Current %s: %s", isFund ? "NAV" : "price",
                               fmtMoney(as.price).c_str());
        }
        ImGui::InputText(isFund ? "New NAV" : "New price", b.price, sizeof b.price);
        finish(formFooter(a) && submitSetPrice(a));
    }
}
