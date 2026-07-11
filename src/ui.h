#pragma once
// Shared UI state, palette and small widget helpers for the ui / tabs / forms
// translation units. main.cpp only needs drawUI/applyStyle/loadFonts/openForm.
#include <cstdint>
#include <string>
#include <vector>
#include "imgui.h"
#include "model.h"

// ---- palette ---------------------------------------------------------------
inline ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}
inline const ImVec4 C_BG = rgb(20, 24, 29);
inline const ImVec4 C_PANEL = rgb(29, 35, 43);
inline const ImVec4 C_PANEL2 = rgb(38, 46, 56);
inline const ImVec4 C_BORDER = rgb(55, 65, 78);
inline const ImVec4 C_TEXT = rgb(232, 234, 237);
inline const ImVec4 C_DIM = rgb(145, 155, 167);
inline const ImVec4 C_GREEN = rgb(46, 204, 113);
inline const ImVec4 C_RED = rgb(235, 87, 70);
inline const ImVec4 C_BLUE = rgb(82, 152, 219);
inline const ImVec4 C_YELLOW = rgb(241, 196, 15);
inline const ImVec4 C_ORANGE = rgb(230, 126, 34);
inline const ImVec4 C_PURPLE = rgb(165, 105, 189);
inline const ImVec4 C_DARK = rgb(15, 18, 22);

extern ImFont* fBody;
extern ImFont* fBig;
extern ImFont* fHuge;

// ---- app state ---------------------------------------------------------------
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
    uint64_t statusAt = 0;
};

// ---- shared helpers (ui.cpp) -------------------------------------------------
void setStatus(App& a, const std::string& s);
Account* selAccount(App& a);  // clamps selAcc; null when there are no accounts
const char* accTypeName(AccountType t);
void TextRight(const std::string& s, const ImVec4& col = C_TEXT, ImFont* font = nullptr);
bool AccentButton(const char* label, const ImVec4& bg, const ImVec4& fg,
                  ImVec2 size = ImVec2(0, 0));

// ---- forms (forms.cpp) -------------------------------------------------------
void openForm(App& a, FormKind k);
void drawForms(App& a);  // all modal dialogs; call once per frame

// ---- tabs (tabs.cpp) ---------------------------------------------------------
void tabGlobal(App& a);
void tabMovements(App& a);
void tabInvest(App& a);
void tabTimeline(App& a);

// ---- shell (ui.cpp) ----------------------------------------------------------
void drawUI(App& a);  // full-window UI: header, tab bar, status bar, modals
void applyStyle();
void loadFonts();
