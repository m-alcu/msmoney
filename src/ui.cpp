// Shared widget helpers, style/fonts, and the top-level window shell.
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include "ui.h"

ImFont* fBody = nullptr;
ImFont* fBig = nullptr;
ImFont* fHuge = nullptr;

void setStatus(App& a, const std::string& s) {
    a.status = s;
    a.statusAt = SDL_GetTicks();
}

Account* selAccount(App& a) {
    if (a.pf.accounts.empty()) return nullptr;
    a.selAcc = std::clamp(a.selAcc, 0, (int)a.pf.accounts.size() - 1);
    return &a.pf.accounts[a.selAcc];
}

const char* accTypeName(AccountType t) {
    switch (t) {
        case AccountType::Cash: return "Cash";
        case AccountType::Bank: return "Bank";
        default: return "Deposit";
    }
}

void TextRight(const std::string& s, const ImVec4& col, ImFont* font) {
    if (font) ImGui::PushFont(font);
    float off = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(s.c_str()).x;
    // never move left of the cell start: overflowing text would get clipped
    // on its left side, silently dropping leading digits
    if (off > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
    ImGui::TextColored(col, "%s", s.c_str());
    if (font) ImGui::PopFont();
}

bool AccentButton(const char* label, const ImVec4& bg, const ImVec4& fg, ImVec2 size) {
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

// ---- main UI ---------------------------------------------------------------
void drawUI(App& a) {
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

void applyStyle() {
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

void loadFonts() {
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
