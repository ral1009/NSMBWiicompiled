#include "disc_setup.h"
#include "runtime_config.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
enum class Pick { Image, Folder, Textures };

struct Selection {
    std::atomic<bool> ready{false};
    fs::path path;
    std::string error;
    Pick kind = Pick::Image;
    bool open = false;

    void show(SDL_Window* window, Pick choice) {
        kind = choice;
        open = true;
        path.clear();
        error.clear();
        auto callback = +[](void* data, const char* const* paths, int) {
            auto& self = *static_cast<Selection*>(data);
            if (!paths) self.error = SDL_GetError();
            else if (*paths) self.path = *paths;
            self.ready.store(true);
        };
        static const SDL_DialogFileFilter formats[] = {{"Wii disc images", "iso;rvz;wbfs"}};
        if (choice == Pick::Image)
            SDL_ShowOpenFileDialog(callback, this, window, formats, 1, nullptr, false);
        else
            SDL_ShowOpenFolderDialog(callback, this, window, nullptr, false);
    }
};

bool ShiftAtStartup() {
    // Wayland delivers the current modifiers when a mapped surface receives keyboard focus.
    const auto until = SDL_GetTicks() + 350;
    while (SDL_GetTicks() < until) {
        SDL_PumpEvents();
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if ((SDL_GetModState() & SDL_KMOD_SHIFT) || keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
            return true;
        SDL_Delay(8);
    }
    return false;
}

LinuxGame SetupWindow(LinuxGame game, bool fastCpu, bool force, std::string message) {
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    struct QuitVideo { ~QuitVideo() { SDL_Quit(); } } video;
    auto* window = SDL_CreateWindow("NSMBWCompiled Setup", 570, 360, SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) throw std::runtime_error(SDL_GetError());
    auto* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) throw std::runtime_error(SDL_GetError());
    SDL_SetRenderDrawColor(renderer, 30, 32, 36, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    if (!force && !game.disc.empty() && !ShiftAtStartup()) return game;

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {18, 16};
    style.ItemSpacing = {10, 7};
    style.FramePadding = {10, 5};
    style.FrameRounding = 3;
    for (const char* font : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                             "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf"}) {
        if (fs::is_regular_file(font)) { io.Fonts->AddFontFromFileTTF(font, 16); break; }
    }
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    Selection selection;
    std::future<LinuxGame> import;
    bool play = false;
    bool close = false;
    bool installed = fs::is_directory(LinuxTexturePack()) && !fs::is_empty(LinuxTexturePack());
    bool useTextures = RuntimeConfigFile::TextureReplacements(false);
    while (!(close && !selection.open && !import.valid()) && !play) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) close = true;
        }
        if (selection.ready.exchange(false)) {
            selection.open = false;
            message = selection.error;
            if (!selection.path.empty() && !close) {
                const bool pack = selection.kind == Pick::Textures;
                message = pack ? "Copying the texture pack..." : "Reading game files...";
                if (selection.kind == Pick::Image) message = "Extracting your disc. This can take a minute...";
                import = std::async(std::launch::async, [path = selection.path, pack, fastCpu, game] {
                    if (!pack) return ImportLinuxDisc(path, fastCpu);
                    InstallLinuxTexturePack(path);
                    return game;
                });
            }
        }
        if (import.valid() && import.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                game = import.get();
                message.clear();
                installed = fs::is_directory(LinuxTexturePack()) && !fs::is_empty(LinuxTexturePack());
                useTextures = RuntimeConfigFile::TextureReplacements(false);
            } catch (const std::exception& error) {
                message = error.what();
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Setup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginDisabled(selection.open || import.valid());
        ImGui::SeparatorText("Game files");
        if (game.disc.empty()) ImGui::TextWrapped("Select your Wii disc image or an extracted disc folder.");
        else {
            ImGui::TextUnformatted(game.version == "E1" ? "USA - revision 1" : "PAL - revision 1");
            ImGui::TextUnformatted(game.disc.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", game.disc.c_str());
        }
        if (ImGui::Button("Disc image...")) selection.show(window, Pick::Image);
        ImGui::SameLine();
        if (ImGui::Button("Extracted folder...")) selection.show(window, Pick::Folder);
        ImGui::Spacing();
        ImGui::SeparatorText("HD texture pack (optional)");
        if (ImGui::Button("Install pack...")) selection.show(window, Pick::Textures);
        ImGui::SameLine();
        ImGui::BeginDisabled(!installed);
        if (ImGui::Checkbox("Use HD textures", &useTextures) && !RuntimeConfigFile::SetTextureReplacements(useTextures))
            message = "Could not save the texture setting.";
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::TextWrapped("%s", message.empty() ? "Hold Shift at startup to open setup again." : message.c_str());
        ImGui::Spacing();
        ImGui::BeginDisabled(game.disc.empty());
        if (ImGui::Button("Play", {96, 0})) play = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Quit", {96, 0})) close = true;
        ImGui::EndDisabled();
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    return play ? game : LinuxGame{};
}
} // namespace

LinuxGame PrepareLinuxGame(const fs::path& input, bool showSetup, bool unattended, bool fastCpu) {
    LinuxGame game;
    std::string message;
    if (!input.empty()) game = ImportLinuxDisc(input, fastCpu);
    else {
        const auto saved = RuntimeConfigFile::ResolvedDvdRoot();
        if (!saved.empty()) {
            try { game = CheckLinuxDisc(saved, fastCpu); }
            catch (const std::exception& error) { message = error.what(); }
        }
        if (game.disc.empty()) {
            const auto discs = RuntimeConfigFile::ApplicationDataDirectory() / "discs";
            for (auto version : {"E1", "P1"}) {
                if (!fs::is_directory(discs / version)) continue;
                try { game = ImportLinuxDisc(discs / version, fastCpu); break; }
                catch (const std::exception& error) { message = error.what(); }
            }
        }
    }
    if (const char* pack = std::getenv("NSMBW_SETUP_TEXTURES"); pack && *pack) {
        InstallLinuxTexturePack(pack);
        unattended = true;
    }
    if (unattended && !showSetup) {
        if (game.disc.empty()) throw std::runtime_error(message.empty() ? "No supported disc configured." : message);
        return game;
    }
    return SetupWindow(game, fastCpu, showSetup, message);
}
