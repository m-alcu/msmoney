// msmoney - a tiny Microsoft Money-style personal finance manager
// SDL3 + Dear ImGui (SDL_Renderer backend), C++17.
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "model.h"

// ---- palette ---------------------------------------------------------------
static ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}
static const ImVec4 C_BG = rgb(20, 24, 29);
static const ImVec4 C_PANEL = rgb(29, 35, 43);
static const ImVec4 C_PANEL2 = rgb(38, 46, 56);
static const ImVec4 C_BORDER = rgb(55, 65, 78);
static const ImVec4 C_TEXT = rgb(232, 234, 237);
static const ImVec4 C_DIM = rgb(145, 155, 167);
static const ImVec4 C_GREEN = rgb(46, 204, 113);
static const ImVec4 C_RED = rgb(235, 87, 70);
static const ImVec4 C_BLUE = rgb(82, 152, 219);
static const ImVec4 C_YELLOW = rgb(241, 196, 15);
static const ImVec4 C_ORANGE = rgb(230, 126, 34);
static const ImVec4 C_PURPLE = rgb(165, 105, 189);
static const ImVec4 C_DARK = rgb(15, 18, 22);

static ImFont* fBody = nullptr;
static ImFont* fBig = nullptr;
static ImFont* fHuge = nullptr;

// ---- app state -------------------------------------------------------------
enum class FormKind {
    None, AddMovement, NewAccount, Buy, Sell, SetPrice, EditDeposit,
    DeleteAccount, DeleteAsset, DeleteMovement, DeleteSnapshot
};

struct FormBufs {
    char date[16]{}, desc[128]{}, name[64]{}, amount[32]{}, units[32]{}, price[32]{},
        initial[32]{}, rate[16]{};
    int typeIdx = 0;   // account or asset type
    int assetIdx = 0;  // sell / update-price target
    std::string error;
};

struct App {
    Portfolio pf;
    std::string path = "msmoney.dat";
    int selAcc = 0;
    int selAsset = -1;
    int selTx = -1;    // movement pending deletion
    int selSnap = -1;  // snapshot pending deletion
    int forceTab = -1;       // one-shot programmatic tab switch
    bool scrollEnd = false;  // one-shot: scroll movement list to bottom
    FormKind pending = FormKind::None;
    FormBufs bufs;
    std::string status;
    Uint64 statusAt = 0;
};

static void setStatus(App& a, const std::string& s) {
    a.status = s;
    a.statusAt = SDL_GetTicks();
}

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

static Account* selAccount(App& a) {
    if (a.pf.accounts.empty()) return nullptr;
    a.selAcc = std::clamp(a.selAcc, 0, (int)a.pf.accounts.size() - 1);
    return &a.pf.accounts[a.selAcc];
}

static const char* accTypeName(AccountType t) {
    switch (t) {
        case AccountType::Cash: return "Cash";
        case AccountType::Bank: return "Bank";
        default: return "Deposit";
    }
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

// ---- small widget helpers --------------------------------------------------
static void TextRight(const std::string& s, const ImVec4& col = C_TEXT, ImFont* font = nullptr) {
    if (font) ImGui::PushFont(font);
    float off = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(s.c_str()).x;
    // never move left of the cell start: overflowing text would get clipped
    // on its left side, silently dropping leading digits
    if (off > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
    ImGui::TextColored(col, "%s", s.c_str());
    if (font) ImGui::PopFont();
}

static bool AccentButton(const char* label, const ImVec4& bg, const ImVec4& fg,
                         ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(bg.x * 1.15f, bg.y * 1.15f, bg.z * 1.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(bg.x * 0.9f, bg.y * 0.9f, bg.z * 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
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
static void openForm(App& a, FormKind k) {
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

static void drawForms(App& a) {
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

// ---- global position tab ---------------------------------------------------
struct SRow {
    std::string name;
    double value;
    std::string extra;
    ImVec4 extraColor;
};

static void section(const char* title, const ImVec4& accent, const std::vector<SRow>& rows,
                    double subtotal) {
    ImGui::TextColored(accent, "%s", title);
    ImGui::Separator();
    if (ImGui::BeginTable(title, 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("extra", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        for (const auto& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.name.c_str());
            ImGui::TableNextColumn();
            if (!r.extra.empty()) TextRight(r.extra, r.extraColor);
            ImGui::TableNextColumn();
            TextRight(fmtMoney(r.value));
        }
        if (rows.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(C_DIM, "(none)");
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(C_DIM, "Subtotal");
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        TextRight(fmtMoney(subtotal), accent);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::Spacing();
}

static void tabGlobal(App& a) {
    Portfolio& pf = a.pf;
    ImGui::Spacing();
    ImGui::TextColored(C_DIM, "TOTAL NET WORTH");
    ImGui::PushFont(fHuge);
    ImGui::TextColored(C_GREEN, "%s", fmtMoney(pf.netWorth()).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    TextRight(todayStr(), C_DIM);
    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::BeginTable("cols", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        auto rowsFor = [&](AccountType t) {
            std::vector<SRow> rows;
            for (auto& acc : pf.accounts)
                if (acc.type == t) rows.push_back({acc.name, acc.balance(), "", C_DIM});
            return rows;
        };
        section("CASH", C_GREEN, rowsFor(AccountType::Cash), pf.totalByType(AccountType::Cash));
        section("BANK ACCOUNTS", C_BLUE, rowsFor(AccountType::Bank),
                pf.totalByType(AccountType::Bank));
        std::vector<SRow> deps;
        for (auto& acc : pf.accounts)
            if (acc.type == AccountType::Deposit)
                deps.push_back({acc.name, acc.balance(),
                                acc.rate > 0 ? fmtNum(acc.rate, 2) + "%" : "no rate set",
                                acc.rate > 0 ? C_DIM : C_ORANGE});
        double accr = pf.accruedInterest();
        if (accr > 0.005)
            deps.push_back({"Accrued interest (unpaid)", accr, "", C_DIM});
        section("DEPOSITS", C_YELLOW, deps, pf.totalByType(AccountType::Deposit) + accr);

        ImGui::TableNextColumn();
        std::vector<SRow> inv;
        for (auto& s : pf.assets) {
            if (s.units <= 0) continue;
            double g = s.gainPct();
            inv.push_back({s.name + (s.type == AssetType::Fund ? "  [F]" : "  [S]"), s.value(),
                           (g >= 0 ? "+" : "") + fmtNum(g, 1) + "%", g >= 0 ? C_GREEN : C_RED});
        }
        section("INVESTMENTS (STOCKS & FUNDS)", C_ORANGE, inv, pf.investmentsValue());

        // asset allocation bar
        double cash = std::max(0.0, pf.totalByType(AccountType::Cash));
        double bank = std::max(0.0, pf.totalByType(AccountType::Bank));
        double depo = std::max(0.0, pf.totalByType(AccountType::Deposit) +
                                        pf.accruedInterest());
        double stocks = 0, funds = 0;
        for (auto& s : pf.assets)
            (s.type == AssetType::Stock ? stocks : funds) += std::max(0.0, s.value());
        double total = cash + bank + depo + stocks + funds;

        ImGui::TextColored(C_PURPLE, "ASSET ALLOCATION");
        ImGui::Separator();
        ImGui::Spacing();
        struct Seg { const char* name; double v; ImVec4 c; };
        Seg segs[5] = {{"Cash", cash, C_GREEN}, {"Bank", bank, C_BLUE},
                       {"Deposits", depo, C_YELLOW}, {"Stocks", stocks, C_ORANGE},
                       {"Funds", funds, C_PURPLE}};
        if (total > 0) {
            float w = ImGui::GetContentRegionAvail().x;
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float bx = p.x;
            for (auto& s : segs) {
                float sw = (float)(s.v / total) * w;
                if (sw > 0.5f)
                    dl->AddRectFilled(ImVec2(bx, p.y), ImVec2(bx + sw, p.y + 26),
                                      ImGui::ColorConvertFloat4ToU32(s.c));
                bx += sw;
            }
            dl->AddRect(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y + 26),
                        ImGui::ColorConvertFloat4ToU32(C_BORDER));
            ImGui::Dummy(ImVec2(w, 32));
            if (ImGui::BeginTable("legend", 2, ImGuiTableFlags_SizingStretchProp)) {
                for (auto& s : segs) {
                    if (s.v <= 0) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImVec2 lp = ImGui::GetCursorScreenPos();
                    dl->AddRectFilled(ImVec2(lp.x, lp.y + 3), ImVec2(lp.x + 12, lp.y + 15),
                                      ImGui::ColorConvertFloat4ToU32(s.c));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
                    ImGui::TextUnformatted(s.name);
                    ImGui::TableNextColumn();
                    TextRight(fmtMoney(s.v) + "  (" + fmtNum(s.v / total * 100, 1) + "%)", C_DIM);
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndTable();
    }
}

// ---- movements tab ---------------------------------------------------------
static void tabMovements(App& a) {
    Portfolio& pf = a.pf;
    ImGui::Spacing();

    // left: account list
    ImGui::BeginChild("accounts", ImVec2(300, 0), ImGuiChildFlags_Borders);
    ImGui::TextColored(C_DIM, "ACCOUNTS");
    ImGui::Separator();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < (int)pf.accounts.size(); i++) {
        Account& acc = pf.accounts[i];
        bool sel = i == a.selAcc;
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##acc", sel, 0, ImVec2(0, 44))) {
            a.selAcc = i;
            a.scrollEnd = true;
        }
        dl->AddText(fBig, fBig->FontSize, ImVec2(p.x + 6, p.y + 2),
                    ImGui::ColorConvertFloat4ToU32(sel ? C_GREEN : C_TEXT), acc.name.c_str());
        dl->AddText(ImVec2(p.x + 6, p.y + 27), ImGui::ColorConvertFloat4ToU32(C_DIM),
                    accTypeName(acc.type));
        std::string bal = fmtMoney(acc.balance());
        float bw = ImGui::CalcTextSize(bal.c_str()).x;
        dl->AddText(ImVec2(p.x + w - bw - 6, p.y + 25),
                    ImGui::ColorConvertFloat4ToU32(C_TEXT), bal.c_str());
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button("+ New account", ImVec2(-1, 0))) openForm(a, FormKind::NewAccount);
    ImGui::EndChild();

    ImGui::SameLine();

    // right: movement register
    ImGui::BeginChild("register", ImVec2(0, 0));
    Account* acc = selAccount(a);
    if (!acc) {
        ImGui::TextColored(C_DIM, "No accounts yet.");
        ImGui::EndChild();
        return;
    }
    bool isDeposit = acc->type == AccountType::Deposit;
    ImGui::PushFont(fBig);
    ImGui::TextUnformatted(acc->name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    if (isDeposit)
        ImGui::TextColored(C_DIM, " Deposit");
    else
        ImGui::TextColored(C_DIM, " %s account - %d movements", accTypeName(acc->type),
                           (int)acc->txs.size());
    ImGui::SameLine();
    {
        // balance + buttons right-aligned
        std::string bal = fmtMoney(acc->balance());
        ImGui::PushFont(fBig);
        float bw = ImGui::CalcTextSize(bal.c_str()).x;
        ImGui::PopFont();
        float btnW = (isDeposit ? 0 : 146) + 106;  // [+ Movement] + [Delete]
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - bw -
                             btnW - 8);
        ImGui::PushFont(fBig);
        ImGui::TextColored(C_GREEN, "%s", bal.c_str());
        ImGui::PopFont();
        if (!isDeposit) {
            ImGui::SameLine();
            if (AccentButton("+ Movement", C_GREEN, C_DARK, ImVec2(130, 0)))
                openForm(a, FormKind::AddMovement);
        }
        ImGui::SameLine();
        if (AccentButton("Delete", C_RED, C_TEXT, ImVec2(90, 0)))
            openForm(a, FormKind::DeleteAccount);
    }
    ImGui::Spacing();

    if (isDeposit) {
        // deposits have no movement register: show the terms and accrual detail
        ImGui::BeginChild("depcard", ImVec2(460, 0), ImGuiChildFlags_Borders |
                                                         ImGuiChildFlags_AutoResizeY);
        ImGui::TextColored(C_YELLOW, "DEPOSIT DETAILS");
        ImGui::Separator();
        if (ImGui::BeginTable("dep", 2)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
            auto row = [](const char* k, const std::string& v, const ImVec4& col = C_TEXT) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(C_DIM, "%s", k);
                ImGui::TableNextColumn();
                TextRight(v, col);
            };
            row("Principal", fmtMoney(acc->balance()));
            if (acc->rate > 0) {
                row("Annual interest rate", fmtNum(acc->rate, 2) + "%");
                row("Accruing since", acc->since);
                row("Accrued unpaid interest", fmtMoney(acc->accrued()), C_YELLOW);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(C_DIM, "Value incl. accrued");
                ImGui::TableNextColumn();
                TextRight(fmtMoney(acc->balance() + acc->accrued()), C_GREEN, fBig);
            } else {
                row("Annual interest rate", "not set", C_ORANGE);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (AccentButton("Edit terms", C_YELLOW, C_DARK, ImVec2(130, 0)))
            openForm(a, FormKind::EditDeposit);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(C_DIM, "When interest is paid out, add a movement to a bank "
                                  "account and move the accrual date forward here.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::EndChild();
        return;
    }

    float footerH = ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("txs", ImVec2(0, -footerH));
    if (ImGui::BeginTable("txtable", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Balance", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();

        double run = acc->initial;
        for (int i = 0; i < (int)acc->txs.size(); i++) {
            Transaction& t = acc->txs[i];
            run += t.amount;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(C_DIM, "%s", t.date.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.desc.c_str());
            ImGui::TableNextColumn();
            TextRight(fmtMoney(t.amount), t.amount < 0 ? C_RED : C_GREEN);
            ImGui::TableNextColumn();
            TextRight(fmtMoney(run));
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, C_RED);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(255, 110, 92));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(200, 70, 55));
            if (ImGui::SmallButton("x")) {
                a.selTx = i;
                openForm(a, FormKind::DeleteMovement);
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this movement");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (acc->txs.empty()) ImGui::TextColored(C_DIM, "No movements yet.");
    if (a.scrollEnd) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
        a.scrollEnd = false;
    }
    ImGui::EndChild();
    TextRight("Initial balance: " + fmtMoney(acc->initial), C_DIM);
    ImGui::EndChild();
}

// ---- investments tab -------------------------------------------------------
static void tabInvest(App& a) {
    Portfolio& pf = a.pf;
    ImGui::Spacing();
    ImGui::PushFont(fBig);
    ImGui::TextUnformatted("Stocks & Funds");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 4 * 122 -
                         8);
    if (AccentButton("Buy", C_GREEN, C_DARK, ImVec2(112, 0))) openForm(a, FormKind::Buy);
    ImGui::SameLine();
    if (AccentButton("Sell", C_ORANGE, C_DARK, ImVec2(112, 0))) openForm(a, FormKind::Sell);
    ImGui::SameLine();
    if (ImGui::Button("Update price", ImVec2(112, 0))) openForm(a, FormKind::SetPrice);
    ImGui::SameLine();
    ImGui::BeginDisabled(a.selAsset < 0);
    if (AccentButton("Delete", C_RED, C_TEXT, ImVec2(112, 0)))
        openForm(a, FormKind::DeleteAsset);
    ImGui::EndDisabled();
    if (a.selAsset < 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Select an asset row first");
    ImGui::Spacing();

    float totalsH = ImGui::GetTextLineHeightWithSpacing() + 14;
    bool hasSel = a.selAsset >= 0 && a.selAsset < (int)pf.assets.size();
    float histH = hasSel ? 210.0f : 0.0f;
    if (ImGui::BeginTable("assets", 9,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, -(totalsH + histH)))) {
        ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Units", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Avg buy", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Price/NAV", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Realized", ImGuiTableColumnFlags_WidthFixed, 105.0f);
        ImGui::TableSetupColumn("Gain", ImGuiTableColumnFlags_WidthFixed, 105.0f);
        ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();

        for (int i = 0; i < (int)pf.assets.size(); i++) {
            Asset& s = pf.assets[i];
            bool sel = i == a.selAsset;
            double realized = s.realizedGain();
            ImVec4 gc = s.gain() >= 0 ? C_GREEN : C_RED;
            ImVec4 rc = realized >= 0 ? C_GREEN : C_RED;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, sel ? C_GREEN : C_TEXT);
            if (ImGui::Selectable(s.name.c_str(), sel,
                                  ImGuiSelectableFlags_SpanAllColumns))
                a.selAsset = sel ? -1 : i;
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::TextColored(C_DIM, "%s", s.type == AssetType::Stock ? "Stock" : "Fund");
            ImGui::TableNextColumn();
            TextRight(fmtNum(s.units, 2));
            ImGui::TableNextColumn();
            TextRight(fmtMoney(s.avgPrice), C_DIM);
            ImGui::TableNextColumn();
            TextRight(fmtMoney(s.price));
            ImGui::TableNextColumn();
            TextRight(fmtMoney(s.value()));
            ImGui::TableNextColumn();
            if (realized != 0) TextRight(fmtMoney(realized), rc);
            ImGui::TableNextColumn();
            TextRight(fmtMoney(s.gain()), gc);
            ImGui::TableNextColumn();
            TextRight((s.gain() >= 0 ? "+" : "") + fmtNum(s.gainPct(), 1), gc);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (pf.assets.empty()) ImGui::TextColored(C_DIM, "No assets yet. Click Buy.");

    // movement history of the selected asset. Note: clicking the selected row
    // toggles the selection off mid-frame, so hasSel (computed before the
    // table loop) may be stale here - re-check the index before using it.
    if (a.selAsset >= 0 && a.selAsset < (int)pf.assets.size()) {
        Asset& s = pf.assets[a.selAsset];
        ImGui::BeginChild("history", ImVec2(0, histH - 8), ImGuiChildFlags_Borders);
        ImGui::TextColored(C_ORANGE, "MOVEMENTS - %s", s.name.c_str());
        ImGui::SameLine();
        double realized = s.realizedGain();
        TextRight("realized " + fmtMoney(realized) + "   unrealized " + fmtMoney(s.gain()) +
                      "   total " + fmtMoney(s.totalGain()),
                  s.totalGain() >= 0 ? C_GREEN : C_RED);
        ImGui::Separator();
        if (ImGui::BeginTable("atx", 6,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Operation", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Units", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Realized P/L", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            double u = 0, avg = 0;  // running position for per-sell realized P/L
            for (auto& t : s.txs) {
                bool buy = t.units > 0;
                double rowRealized = 0;
                if (buy) {
                    avg = (u * avg + t.units * t.price) / (u + t.units);
                    u += t.units;
                } else {
                    double sold = std::min(-t.units, u);
                    rowRealized = sold * (t.price - avg);
                    u -= sold;
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(C_DIM, "%s", t.date.empty() ? "-" : t.date.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(buy ? C_GREEN : C_ORANGE,
                                   buy ? (t.date.empty() ? "Opening position" : "Buy")
                                       : "Sell");
                ImGui::TableNextColumn();
                TextRight(fmtNum(std::fabs(t.units), 2));
                ImGui::TableNextColumn();
                TextRight(fmtMoney(t.price));
                ImGui::TableNextColumn();
                TextRight(fmtMoney(std::fabs(t.amount())), buy ? C_TEXT : C_DIM);
                ImGui::TableNextColumn();
                if (!buy)
                    TextRight(fmtMoney(rowRealized), rowRealized >= 0 ? C_GREEN : C_RED);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    // totals
    double cost = pf.investmentsCost(), value = pf.investmentsValue();
    double realizedTotal = 0;
    for (auto& s : pf.assets) realizedTotal += s.realizedGain();
    double total = value - cost + realizedTotal;
    ImVec4 gc = total >= 0 ? C_GREEN : C_RED;
    ImGui::Separator();
    ImGui::TextColored(C_DIM, "TOTAL  (invested %s)", fmtMoney(cost).c_str());
    ImGui::SameLine();
    TextRight(fmtMoney(value) + "   realized " + fmtMoney(realizedTotal) + "   unrealized " +
                  fmtMoney(value - cost) + "   total " + fmtMoney(total),
              gc, fBig);
}

// ---- timeline tab ------------------------------------------------------------
// money label for chart axes: like fmtMoney but without the cents
static std::string fmtAxis(double v) {
    std::string s = fmtMoney(v);
    size_t dot = s.rfind('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
}

// smallest 1/2/5 * 10^k step that splits range into at most maxTicks intervals
static double niceStep(double range, int maxTicks) {
    if (range <= 0) return 1.0;
    double raw = range / std::max(1, maxTicks);
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 5.0})
        if (mag * m >= raw) return mag * m;
    return mag * 10.0;
}

static void tabTimeline(App& a) {
    Portfolio& pf = a.pf;
    const auto& sn = pf.snapshots;
    ImGui::Spacing();
    ImGui::PushFont(fBig);
    ImGui::TextUnformatted("Timeline");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(C_DIM, " asset allocation per snapshot - %d stored", (int)sn.size());
    ImGui::Spacing();
    if (sn.empty()) {
        ImGui::TextColored(C_DIM,
                           "No snapshots yet. Click the Snapshot button (top right) to store\n"
                           "the current position; each snapshot becomes a point on this chart.");
        return;
    }

    struct Series {
        const char* name;
        ImVec4 c;
        double (*get)(const Snapshot&);
        float th;
    };
    const Series series[6] = {
        {"Cash", C_GREEN, [](const Snapshot& s) { return s.cash; }, 1.5f},
        {"Bank", C_BLUE, [](const Snapshot& s) { return s.bank; }, 1.5f},
        {"Deposits", C_YELLOW, [](const Snapshot& s) { return s.deposits; }, 1.5f},
        {"Stocks", C_ORANGE, [](const Snapshot& s) { return s.stocks; }, 1.5f},
        {"Funds", C_PURPLE, [](const Snapshot& s) { return s.funds; }, 1.5f},
        {"Total", C_TEXT, [](const Snapshot& s) { return s.total(); }, 3.0f},
    };

    int n = (int)sn.size();
    std::vector<double> xs(n);
    for (int i = 0; i < n; i++) xs[i] = (double)daysBetween(sn.front().date, sn[i].date);
    double spanX = std::max(1.0, xs.back());

    double maxV = 0, minV = 0;
    for (const auto& s : sn)
        for (const auto& sr : series) {
            maxV = std::max(maxV, sr.get(s));
            minV = std::min(minV, sr.get(s));
        }
    double step = niceStep((maxV - minV) * 1.05, 5);
    double yLo = std::floor(minV / step) * step;
    double yHi = std::ceil((maxV + step * 0.2) / step) * step;

    float tableH = 200.0f;
    ImGui::BeginChild("chart", ImVec2(0, -tableH), ImGuiChildFlags_Borders);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float legendH = ImGui::GetTextLineHeightWithSpacing() + 8;
    ImVec2 pl0(p0.x + 92, p0.y + 10);                            // plot area
    ImVec2 pl1(p0.x + avail.x - 18, p0.y + avail.y - 24 - legendH);
    if (pl1.x > pl0.x + 60 && pl1.y > pl0.y + 60) {
        auto px = [&](double x) {
            if (n == 1) return (pl0.x + pl1.x) * 0.5f;  // single point: center it
            return pl0.x + (float)(x / spanX) * (pl1.x - pl0.x);
        };
        auto py = [&](double v) {
            return pl1.y - (float)((v - yLo) / (yHi - yLo)) * (pl1.y - pl0.y);
        };
        ImU32 cGrid = ImGui::ColorConvertFloat4ToU32(rgb(45, 53, 64));
        ImU32 cDim = ImGui::ColorConvertFloat4ToU32(C_DIM);

        for (double v = yLo; v <= yHi + step * 0.01; v += step) {
            float y = py(v);
            dl->AddLine(ImVec2(pl0.x, y), ImVec2(pl1.x, y), cGrid);
            std::string lab = fmtAxis(v);
            float tw = ImGui::CalcTextSize(lab.c_str()).x;
            dl->AddText(ImVec2(pl0.x - tw - 8, y - ImGui::GetTextLineHeight() * 0.5f), cDim,
                        lab.c_str());
        }
        // date labels under their snapshot, skipping any that would overlap
        float lastLabEnd = -1e9f;
        for (int i = 0; i < n; i++) {
            float x = px(xs[i]);
            float tw = ImGui::CalcTextSize(sn[i].date.c_str()).x;
            float lx = std::clamp(x - tw * 0.5f, pl0.x - 40.0f, pl1.x - tw + 12.0f);
            if (lx < lastLabEnd + 24) continue;
            dl->AddLine(ImVec2(x, pl0.y), ImVec2(x, pl1.y), cGrid);
            dl->AddText(ImVec2(lx, pl1.y + 6), cDim, sn[i].date.c_str());
            lastLabEnd = lx + tw;
        }
        dl->AddRect(pl0, pl1, ImGui::ColorConvertFloat4ToU32(C_BORDER));

        std::vector<ImVec2> pts(n);
        for (const auto& sr : series) {
            ImU32 col = ImGui::ColorConvertFloat4ToU32(sr.c);
            for (int i = 0; i < n; i++) pts[i] = ImVec2(px(xs[i]), py(sr.get(sn[i])));
            if (n > 1) dl->AddPolyline(pts.data(), n, col, 0, sr.th);
            for (int i = 0; i < n; i++)
                dl->AddCircleFilled(pts[i], sr.th >= 3.0f ? 4.0f : 3.0f, col);
        }
        // hover: vertical marker + tooltip with the nearest snapshot's figures
        if (ImGui::IsMouseHoveringRect(pl0, pl1)) {
            float mx = ImGui::GetIO().MousePos.x;
            int best = 0;
            for (int i = 1; i < n; i++)
                if (std::fabs(px(xs[i]) - mx) < std::fabs(px(xs[best]) - mx)) best = i;
            float x = px(xs[best]);
            dl->AddLine(ImVec2(x, pl0.y), ImVec2(x, pl1.y), cDim);
            ImGui::BeginTooltip();
            ImGui::TextColored(C_DIM, "%s", sn[best].date.c_str());
            ImGui::Separator();
            if (ImGui::BeginTable("tt", 2)) {
                ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                for (int k = 5; k >= 0; k--) {  // total first
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(series[k].c, "%s", series[k].name);
                    ImGui::TableNextColumn();
                    TextRight(fmtMoney(series[k].get(sn[best])));
                }
                ImGui::EndTable();
            }
            ImGui::EndTooltip();
        }
    }
    // legend with the latest snapshot's values, at the bottom of the chart
    ImGui::Dummy(ImVec2(avail.x, avail.y - legendH - ImGui::GetStyle().ItemSpacing.y));
    for (int k = 0; k < 6; k++) {
        if (k) ImGui::SameLine(0, 20);
        ImVec2 lp = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(ImVec2(lp.x, lp.y + 3), ImVec2(lp.x + 12, lp.y + 15),
                          ImGui::ColorConvertFloat4ToU32(series[k].c));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18);
        ImGui::TextUnformatted(series[k].name);
        ImGui::SameLine(0, 6);
        ImGui::TextColored(C_DIM, "%s", fmtMoney(series[k].get(sn.back())).c_str());
    }
    ImGui::EndChild();

    // snapshot history, newest first
    if (ImGui::BeginTable("snaptable", 8,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Cash", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Bank", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Deposits", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Stocks", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Funds", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        for (int i = n - 1; i >= 0; i--) {
            const Snapshot& s = sn[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(C_DIM, "%s", s.date.c_str());
            ImGui::TableNextColumn();
            TextRight(fmtMoney(s.cash));
            ImGui::TableNextColumn();
            TextRight(fmtMoney(s.bank));
            ImGui::TableNextColumn();
            TextRight(fmtMoney(s.deposits));
            ImGui::TableNextColumn();
            TextRight(fmtMoney(s.stocks));
            ImGui::TableNextColumn();
            TextRight(fmtMoney(s.funds));
            ImGui::TableNextColumn();
            TextRight(fmtMoney(s.total()), C_GREEN);
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, C_RED);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(255, 110, 92));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(200, 70, 55));
            if (ImGui::SmallButton("x")) {
                a.selSnap = i;
                openForm(a, FormKind::DeleteSnapshot);
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this snapshot");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ---- main UI ---------------------------------------------------------------
static void drawUI(App& a) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    // keyboard tab switching
    bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                                     ImGuiPopupFlags_AnyPopupLevel);
    if (!popupOpen && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_1)) a.forceTab = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_2)) a.forceTab = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) a.forceTab = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_4)) a.forceTab = 3;
    }

    // header: title + tabs
    ImGui::PushFont(fHuge);
    ImGui::TextColored(C_GREEN, "msmoney");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(C_DIM, "personal finances");
    ImGui::SameLine();
    {
        // global action: record today's position for the Timeline chart
        float btnW = 120.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                             btnW - 8);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
        bool existed = false;
        for (auto& s : a.pf.snapshots) existed |= s.date == todayStr();
        if (AccentButton("Snapshot", C_BLUE, C_DARK, ImVec2(btnW, 0))) {
            a.pf.takeSnapshot();
            a.pf.save(a.path);
            setStatus(a, std::string(existed ? "Snapshot updated for " : "Snapshot stored for ") +
                             todayStr() + "  -  net worth " + fmtMoney(a.pf.netWorth()));
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Store today's position (one per day) for the Timeline tab");
    }

    float statusH = ImGui::GetTextLineHeightWithSpacing() + 6;
    if (ImGui::BeginTabBar("tabs")) {
        const char* names[4] = {"Global Position", "Movements", "Investments", "Timeline"};
        for (int i = 0; i < 4; i++) {
            ImGuiTabItemFlags flags = a.forceTab == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(names[i], nullptr, flags)) {
                ImGui::BeginChild("content", ImVec2(0, -statusH));
                if (i == 0) tabGlobal(a);
                else if (i == 1) tabMovements(a);
                else if (i == 2) tabInvest(a);
                else tabTimeline(a);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    a.forceTab = -1;

    // status bar
    ImGui::Separator();
    if (!a.status.empty() && SDL_GetTicks() - a.statusAt < 5000)
        ImGui::TextColored(C_GREEN, "%s", a.status.c_str());
    else
        ImGui::TextUnformatted(" ");
    ImGui::SameLine();
    TextRight("1/2/3/4: tabs  |  click row: select", C_DIM);

    // open pending popup, then draw all modals
    if (a.pending != FormKind::None) {
        const char* ids[] = {"", "Add movement", "New account", "Buy stock / fund",
                             "Sell stock / fund", "Update price / NAV", "Deposit terms",
                             "Delete account", "Delete asset", "Delete movement",
                             "Delete snapshot"};
        ImGui::OpenPopup(ids[(int)a.pending]);
        a.pending = FormKind::None;
    }
    drawForms(a);

    ImGui::End();
}

static void applyStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 0;
    st.ChildRounding = 6;
    st.FrameRounding = 4;
    st.PopupRounding = 6;
    st.TabRounding = 4;
    st.GrabRounding = 4;
    st.ScrollbarSize = 12;
    st.WindowPadding = ImVec2(16, 12);
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(10, 8);
    st.CellPadding = ImVec2(6, 5);

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = C_BG;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = C_PANEL;
    c[ImGuiCol_Border] = C_BORDER;
    c[ImGuiCol_Text] = C_TEXT;
    c[ImGuiCol_TextDisabled] = C_DIM;
    c[ImGuiCol_FrameBg] = C_DARK;
    c[ImGuiCol_FrameBgHovered] = C_PANEL2;
    c[ImGuiCol_FrameBgActive] = C_PANEL2;
    c[ImGuiCol_Button] = C_PANEL2;
    c[ImGuiCol_ButtonHovered] = rgb(52, 62, 74);
    c[ImGuiCol_ButtonActive] = rgb(46, 56, 68);
    c[ImGuiCol_Header] = C_PANEL2;                       // selected rows
    c[ImGuiCol_HeaderHovered] = rgb(44, 53, 64);
    c[ImGuiCol_HeaderActive] = rgb(50, 60, 72);
    c[ImGuiCol_Tab] = C_PANEL2;
    c[ImGuiCol_TabHovered] = rgb(40, 120, 80);
    c[ImGuiCol_TabSelected] = C_GREEN;
    c[ImGuiCol_TabSelectedOverline] = C_GREEN;
    c[ImGuiCol_TableHeaderBg] = C_PANEL2;
    c[ImGuiCol_TableRowBg] = rgb(25, 30, 37);
    c[ImGuiCol_TableRowBgAlt] = C_BG;
    c[ImGuiCol_TableBorderLight] = rgb(40, 48, 58);
    c[ImGuiCol_TableBorderStrong] = C_BORDER;
    c[ImGuiCol_Separator] = C_BORDER;
    c[ImGuiCol_CheckMark] = C_GREEN;
    c[ImGuiCol_SliderGrab] = C_GREEN;
    c[ImGuiCol_ScrollbarBg] = C_BG;
    c[ImGuiCol_ScrollbarGrab] = C_BORDER;
    c[ImGuiCol_TitleBg] = C_PANEL2;
    c[ImGuiCol_TitleBgActive] = C_PANEL2;
    c[ImGuiCol_TitleBgCollapsed] = C_PANEL2;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.65f);
    c[ImGuiCol_NavCursor] = C_GREEN;
}

static void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* reg = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    const char* bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    auto exists = [](const char* p) {
        if (FILE* f = fopen(p, "rb")) { fclose(f); return true; }
        return false;
    };
    if (exists(reg) && exists(bold)) {
        fBody = io.Fonts->AddFontFromFileTTF(reg, 17.0f);
        fBig = io.Fonts->AddFontFromFileTTF(bold, 23.0f);
        fHuge = io.Fonts->AddFontFromFileTTF(bold, 34.0f);
    }
    if (!fBody) fBody = io.Fonts->AddFontDefault();
    if (!fBig) fBig = fBody;
    if (!fHuge) fHuge = fBody;
}

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* win =
        SDL_CreateWindow("msmoney - Personal Finances", 1200, 760, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    if (!win || !ren) {
        SDL_Log("window/renderer failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);  // best effort; frame pacing no longer relies on it

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // fixed fullscreen layout, nothing to persist
    applyStyle();
    loadFonts();
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

    App a;
    if (!a.pf.load(a.path)) {
        a.pf.seed();
        a.pf.save(a.path);
        setStatus(a, "First run: created sample data in " + a.path);
    } else {
        setStatus(a, "Loaded " + a.path);
    }

    // debug hooks for headless testing / screenshots
    const char* shotPath = SDL_getenv("MSMONEY_SHOT");
    if (const char* t = SDL_getenv("MSMONEY_TAB")) a.forceTab = std::clamp(atoi(t), 0, 3);
    if (const char* s = SDL_getenv("MSMONEY_ACC")) a.selAcc = atoi(s);
    if (const char* s = SDL_getenv("MSMONEY_ASSET")) a.selAsset = atoi(s);
    if (const char* fk = SDL_getenv("MSMONEY_FORM"))
        openForm(a, (FormKind)std::clamp(atoi(fk), 0, 10));
    int frame = 0;

    bool running = true;
    Uint64 lastFrame = 0;
    while (running) {
        // Event-driven loop: sleep until input arrives instead of redrawing
        // at full speed (SDL may claim vsync support without ever blocking in
        // RenderPresent, which pins a CPU core). The timeout keeps slow
        // animations alive: caret blink, status-bar expiry, daily accrual.
        if (!shotPath) SDL_WaitEventTimeout(nullptr, 250);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
        }
        // during bursts of events (mouse move, typing), still cap at ~60 fps
        Uint64 now = SDL_GetTicks();
        if (!shotPath && now - lastFrame < 16) SDL_Delay((Uint32)(16 - (now - lastFrame)));
        lastFrame = SDL_GetTicks();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        drawUI(a);
        ImGui::Render();

        SDL_SetRenderDrawColor(ren, (Uint8)(C_BG.x * 255), (Uint8)(C_BG.y * 255),
                               (Uint8)(C_BG.z * 255), 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);

        if (shotPath && ++frame == 5) {
            SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr);
            if (s) {
                SDL_SaveBMP(s, shotPath);
                SDL_DestroySurface(s);
            }
            running = false;
        }
        SDL_RenderPresent(ren);
    }

    a.pf.save(a.path);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
