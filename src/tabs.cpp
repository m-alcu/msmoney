// The four views: Global Position, Movements, Investments, Timeline.
#include <algorithm>
#include <cmath>
#include "ui.h"

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

void tabGlobal(App& a) {
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
void tabMovements(App& a) {
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
void tabInvest(App& a) {
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

void tabTimeline(App& a) {
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
