// msmoney - a tiny Microsoft Money-style personal finance manager
// SDL3 + Dear ImGui (SDL_Renderer backend), C++17.
//
// Layout of the sources:
//   model.h/.cpp  - domain: accounts, assets, snapshots, persistence (no UI)
//   ui.h/.cpp     - shared UI state/palette/widgets + window shell (drawUI)
//   tabs.cpp      - the four views
//   forms.cpp     - all modal dialogs
//   main.cpp      - SDL init and the event/render loop
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "model.h"
#include "ui.h"

// An optional config.ini in the working directory can move the data file
// somewhere else (e.g. into a synced folder):
//   data = /home/user/Documents/msmoney.dat
// Lines starting with # or ; are comments; ~/ expands to $HOME.
static std::string dataPath() {
    std::ifstream f("config.ini");
    std::string line;
    auto trim = [](std::string s) {
        size_t b = s.find_first_not_of(" \t");
        size_t e = s.find_last_not_of(" \t");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    };
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        if (key == "data" && !val.empty()) {
            if (val[0] == '~' && (val.size() == 1 || val[1] == '/'))
                if (const char* home = getenv("HOME")) val = home + val.substr(1);
            return val;
        }
    }
    return "msmoney.dat";
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
    a.path = dataPath();
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
