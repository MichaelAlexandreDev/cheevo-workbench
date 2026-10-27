// cheevo-workbench entry point: creates the SDL3 window with an OpenGL context, loads the
// libretro core given on the command line and runs the Dear ImGui main loop.

#include "frame_pacer.hpp"
#include "game_view.hpp"
#include "libretro_core.hpp"
#include "paths.hpp"
#include "sdl_audio_output.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h> // DockBuilderGetCentralNode: where the game is drawn

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr const char* kUsage =
    "Usage: cheevo-workbench [--core <core.so>] [--system <bios dir>] [game]\n"
    "\n"
    "  --core <core.so>     libretro core to load, e.g. "
    "/usr/lib/libretro/mednafen_psx_libretro.so\n"
    "  --system <dir>       directory with BIOS files (default: "
    "~/.local/share/cheevo-workbench/system)\n"
    "  game                 content to load into the core (needs --core)\n";

// Command line until the File menu can open cores and games (step 1.7).
struct LaunchOptions {
    std::filesystem::path core;
    std::filesystem::path system_dir;
    std::filesystem::path game;
};

// Returns nullopt when the program should exit (after --help or a usage error).
std::optional<LaunchOptions> parse_args(int argc, char** argv, int& exit_code) {
    LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "-h" || arg == "--help") {
            std::fputs(kUsage, stdout);
            exit_code = 0;
            return std::nullopt;
        } else if (arg == "--core" && has_value) {
            options.core = argv[++i];
        } else if (arg == "--system" && has_value) {
            options.system_dir = argv[++i];
        } else if (!arg.starts_with('-') && options.game.empty()) {
            options.game = argv[i];
        } else {
            std::fprintf(stderr, "Invalid argument: %s\n\n%s", argv[i], kUsage);
            exit_code = 2;
            return std::nullopt;
        }
    }
    if (!options.game.empty() && options.core.empty()) {
        std::fprintf(stderr, "A game needs a core: pass --core <core.so>\n\n%s", kUsage);
        exit_code = 2;
        return std::nullopt;
    }
    return options;
}

// Loads the core and game from the command line. Returns an error message, empty on success.
std::string load_from_options(cw::LibretroCore& core, const LaunchOptions& options) {
    if (options.core.empty()) {
        return {};
    }
    // Default system/save folders live in the data directory, which is empty when there is no
    // usable location; the core is then told "no directory" instead of a random one.
    const std::filesystem::path data = cw::data_dir();
    auto default_dir = [&data](const char* name) -> std::filesystem::path {
        if (data.empty()) {
            return {};
        }
        std::error_code ec;
        std::filesystem::create_directories(data / name, ec);
        return ec ? std::filesystem::path{} : data / name;
    };
    cw::LibretroCore::Directories dirs{
        .system = options.system_dir.empty() ? default_dir("system")
                                             : std::filesystem::absolute(options.system_dir),
        .save = default_dir("saves")};
    SDL_Log("System (BIOS) directory: %s", dirs.system.empty() ? "(none)" : dirs.system.c_str());

    std::string error;
    if (!core.load(options.core, dirs, error)) {
        return "Could not load core: " + error;
    }
    if (!options.game.empty() && !core.load_game(options.game, error)) {
        return "Could not load game: " + error;
    }
    return {};
}

struct UiState {
    bool quit = false;
    bool show_imgui_demo = false;
    bool paused = false; // Pause controls are added in step 1.7.
};

void draw_main_menu(UiState& ui) {
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal)) {
        ui.quit = true;
    }
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
            ui.quit = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Dear ImGui demo", nullptr, &ui.show_imgui_demo);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void draw_status_window(const cw::LibretroCore& core, const std::string& load_error,
                        cw::SdlAudioOutput& audio, const cw::FramePacer& pacer) {
    ImGui::Begin("Status");
    ImGui::Text("cheevo-workbench %s", CW_VERSION);
    ImGui::Separator();

    if (!load_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", load_error.c_str());
    }
    if (!core.is_loaded()) {
        ImGui::TextDisabled("No core loaded.");
    } else {
        const auto& info = core.system_info();
        ImGui::Text("Core: %s %s", info.name.c_str(), info.version.c_str());
        ImGui::Text("Core options: %zu", core.options().options().size());
        if (!core.is_game_loaded()) {
            ImGui::TextDisabled("No game loaded.");
        } else {
            const retro_system_av_info& av = core.av_info();
            ImGui::Text("Geometry: %ux%u (max %ux%u)", av.geometry.base_width,
                        av.geometry.base_height, av.geometry.max_width, av.geometry.max_height);
            ImGui::Text("Last frame: %ux%u, aspect %.3f", core.last_frame_width(),
                        core.last_frame_height(), av.geometry.aspect_ratio);
            ImGui::Text("Images received: %llu",
                        static_cast<unsigned long long>(core.video_frame().serial));
            ImGui::Text("Core timing: %.3f fps, audio %.0f Hz", av.timing.fps,
                        av.timing.sample_rate);
            ImGui::Text("Frames run: %llu", static_cast<unsigned long long>(core.frame_count()));
            ImGui::Text("Measured FPS: %.2f", pacer.measured_fps());
        }
    }

    ImGui::Separator();
    const std::string audio_status = audio.status_text();
    if (audio.has_error()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Audio: %s", audio_status.c_str());
    } else {
        ImGui::Text("Audio: %s", audio_status.c_str());
    }
    ImGui::Text("Audio queue: %.1f ms", audio.queued_seconds() * 1000.0);
    ImGui::Text("Frame pacing: %s",
                pacer.mode() == cw::FramePacer::Mode::Audio ? "audio" : "clock");
    ImGui::Text("UI: %.1f FPS", ImGui::GetIO().Framerate);
    ImGui::End();
}

} // namespace

int main(int argc, char** argv) {
    int exit_code = 0;
    const std::optional<LaunchOptions> launch = parse_args(argc, argv, exit_code);
    if (!launch) {
        return exit_code;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // OpenGL 3.3 core: supported by every Linux desktop driver (Mesa, NVIDIA) and enough for
    // libretro cores that request a hardware-rendered GL context later on.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_Window* window = SDL_CreateWindow(
        "cheevo-workbench", static_cast<int>(1280 * scale), static_cast<int>(800 * scale),
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GL_CreateContext failed: %s",
                     SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Synchronize UI presentation; the core has its own frame pacer.

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    // Window layout is saved next to the rest of the config instead of the working directory;
    // with no usable config directory, it is simply not saved. Must outlive the ImGui context.
    const std::filesystem::path config = cw::config_dir();
    const std::string ini_path = config.empty() ? "" : (config / "imgui.ini").string();
    io.IniFilename = ini_path.empty() ? nullptr : ini_path.c_str();

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;

    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    cw::SdlAudioOutput audio_output;
    audio_output.initialize(); // A missing audio device must not prevent the UI from opening.

    // Loaded after the GL context exists: hardware-rendered cores will need it.
    cw::LibretroCore core;
    core.set_audio_sink(&audio_output);
    cw::GameView game_view;
    const std::string load_error = load_from_options(core, *launch);
    if (!load_error.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", load_error.c_str());
    }

    UiState ui;
    cw::FramePacer frame_pacer;
    while (!ui.quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                ui.quit = true;
            }
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        const auto now = cw::FramePacer::Clock::now();
        frame_pacer.set_core_fps(core.is_game_loaded() ? core.av_info().timing.fps : 0.0, now);
        frame_pacer.set_audio_available(audio_output.available(), now);
        if (frame_pacer.set_paused(ui.paused, now)) {
            audio_output.clear();
        }

        size_t frames_run = 0;
        if (core.is_game_loaded()) {
            const size_t frames_due = frame_pacer.frames_to_run(now, audio_output.queued_seconds());
            for (size_t i = 0; i < frames_due; ++i) {
                core.run_frame();
                ++frames_run;
                if (!audio_output.available() &&
                    frame_pacer.mode() == cw::FramePacer::Mode::Audio) {
                    frame_pacer.set_audio_available(false, cw::FramePacer::Clock::now());
                    break;
                }
            }
        }
        if (frames_run > 0) {
            game_view.update(core.video_frame());
        }
        if (frame_pacer.record_frames(frames_run, cw::FramePacer::Clock::now())) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Core measured %.2f FPS",
                         frame_pacer.measured_fps());
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Passthru keeps the central area of the dock see-through. The game is drawn there on
        // the background draw list, i.e. behind every ImGui window, so docked tool windows
        // around it never cover the image.
        const ImGuiID dockspace =
            ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        if (const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace)) {
            const ImVec2 max(central->Pos.x + central->Size.x, central->Pos.y + central->Size.y);
            game_view.draw(ImGui::GetBackgroundDrawList(), central->Pos, max,
                           core.av_info().geometry.aspect_ratio);
        }
        draw_main_menu(ui);
        draw_status_window(core, load_error, audio_output, frame_pacer);
        if (ui.show_imgui_demo) {
            ImGui::ShowDemoWindow(&ui.show_imgui_demo);
        }

        ImGui::Render();
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                   static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    core.unload(); // before the GL context goes away
    audio_output.shutdown();
    game_view.release();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
