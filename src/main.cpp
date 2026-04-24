#include "GameState.h"
#include "Renderer.h"

#include <SFML/Graphics.hpp>
#include <imgui-SFML.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

std::filesystem::path projectRoot() {
#ifdef STARCRAFT_COMBAT_SIM_ROOT
    return std::filesystem::path(STARCRAFT_COMBAT_SIM_ROOT);
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path binDirectory() {
    return projectRoot() / "bin";
}

std::filesystem::path scenarioDirectory() {
    return binDirectory() / "scenarios";
}

std::filesystem::path defaultDataPath() {
    return binDirectory() / "starcraft_data";
}

std::filesystem::path defaultMapPath(const std::filesystem::path& dataPath) {
    return dataPath / "maps" / "(4)Python.scx";
}

std::filesystem::path normalizePath(const std::filesystem::path& path) {
    try {
        return std::filesystem::weakly_canonical(path);
    } catch (...) {
        return std::filesystem::absolute(path).lexically_normal();
    }
}

std::vector<std::filesystem::path> scanScenarioFiles(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> scenarios;
    if (!std::filesystem::exists(directory)) return scenarios;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".txt") continue;
        scenarios.push_back(normalizePath(entry.path()));
    }

    std::sort(scenarios.begin(), scenarios.end(), [](const auto& a, const auto& b) {
        return a.filename().string() < b.filename().string();
    });
    return scenarios;
}

std::filesystem::path defaultScenarioPath(const std::vector<std::filesystem::path>& scenarios) {
    if (!scenarios.empty()) return scenarios.front();
    return scenarioDirectory() / "marines_4v4.txt";
}

std::string scenarioLabel(const std::filesystem::path& path) {
    std::string label = path.stem().string();
    std::replace(label.begin(), label.end(), '_', ' ');
    return label;
}

bool samePath(const std::filesystem::path& a, const std::filesystem::path& b) {
    return normalizePath(a) == normalizePath(b);
}

void printSummary(const scc::GameState& state) {
    int p0 = 0;
    int p1 = 0;
    for (const auto& unit : state.units()) {
        if (!unit.alive) continue;
        if (unit.player == 0) ++p0;
        if (unit.player == 1) ++p1;
    }
    std::cout << "Frame " << state.frame() << ": player 0 alive=" << p0 << ", player 1 alive=" << p1 << '\n';
}

bool isMouseCameraEvent(const sf::Event& event) {
    return event.is<sf::Event::MouseWheelScrolled>() ||
           event.is<sf::Event::MouseButtonPressed>() ||
           event.is<sf::Event::MouseButtonReleased>() ||
           event.is<sf::Event::MouseMoved>();
}

} // namespace

int main(int argc, char** argv) {
    try {
        bool headless = false;
        int headlessFrames = 480;
        std::vector<std::string> positional;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--headless") {
                headless = true;
            } else if (arg == "--frames" && i + 1 < argc) {
                headlessFrames = std::stoi(argv[++i]);
            } else {
                positional.push_back(std::move(arg));
            }
        }

        const std::filesystem::path scenariosDir = scenarioDirectory();
        std::vector<std::filesystem::path> scenarioFiles = scanScenarioFiles(scenariosDir);
        std::filesystem::path scenarioPath = positional.size() > 0 ? normalizePath(std::filesystem::path(positional[0])) : defaultScenarioPath(scenarioFiles);
        const std::filesystem::path dataPath = positional.size() > 1 ? std::filesystem::path(positional[1]) : defaultDataPath();
        const std::filesystem::path mapPath = positional.size() > 2 ? std::filesystem::path(positional[2]) : defaultMapPath(dataPath);

        std::cout << "Scenario: " << scenarioPath << '\n';
        std::cout << "Scenarios: " << scenariosDir << '\n';
        std::cout << "Data:     " << dataPath << '\n';
        std::cout << "Map:      " << mapPath << '\n';

        scc::GameState state(dataPath, mapPath);
        state.loadScenario(scenarioPath);
        std::cout << "Loaded:   " << state.mapPath() << '\n';
        std::cout << "Script:   " << state.scriptName() << '\n';

        if (headless) {
            for (int i = 0; i < headlessFrames && !state.isTerminal(); ++i) {
                state.update();
            }
            printSummary(state);
            return 0;
        }

        scc::Renderer renderer(state);

        sf::RenderWindow window(sf::VideoMode({1280u, 720u}), "StarcraftCombatSim - OpenBW combat sandbox");
        window.setFramerateLimit(60);
        renderer.centerOnCombat(window, state);
        if (!ImGui::SFML::Init(window)) {
            throw std::runtime_error("Failed to initialize ImGui-SFML");
        }

        sf::Clock clock;
        double accumulator = 0.0;
        int lastPrintedFrame = -1;
        std::string scenarioError;
        bool autoCamera = false;
        bool autoAdvance = false;
        bool showCollisionBoxes = false;
        bool drawGameTerrain = false;
        bool playing = true;
        int simulationFps = 24;

        auto restartScenario = [&](const std::filesystem::path& nextScenarioPath) -> bool {
            try {
                std::cout << "Restarting scenario: " << normalizePath(nextScenarioPath) << std::endl;
                state.loadScenario(nextScenarioPath);
                scenarioPath = normalizePath(nextScenarioPath);
                renderer.reload(window, state);
                accumulator = 0.0;
                lastPrintedFrame = -1;
                scenarioError.clear();
                std::cout << "Scenario: " << scenarioPath << '\n';
                std::cout << "Loaded:   " << state.mapPath() << '\n';
                std::cout << "Script:   " << state.scriptName() << '\n';
                return true;
            } catch (const std::exception& e) {
                scenarioError = e.what();
                std::cerr << "Scenario load failed: " << scenarioError << '\n';
                return false;
            }
        };

        auto nextScenarioPath = [&]() -> std::optional<std::filesystem::path> {
            if (scenarioFiles.empty()) return std::nullopt;
            auto found = std::find_if(scenarioFiles.begin(), scenarioFiles.end(), [&](const std::filesystem::path& candidate) {
                return samePath(candidate, scenarioPath);
            });
            if (found == scenarioFiles.end()) return scenarioFiles.front();
            ++found;
            if (found == scenarioFiles.end()) return scenarioFiles.front();
            return *found;
        };

        while (window.isOpen()) {
            while (const std::optional event = window.pollEvent()) {
                if (event->is<sf::Event::Closed>()) {
                    window.close();
                    continue;
                }
                ImGui::SFML::ProcessEvent(window, *event);
                const bool mustHandleCameraEvent = event->is<sf::Event::Resized>() ||
                                                   event->is<sf::Event::MouseButtonReleased>() ||
                                                   (isMouseCameraEvent(*event) && !ImGui::GetIO().WantCaptureMouse);
                if (mustHandleCameraEvent && (!autoCamera || event->is<sf::Event::Resized>())) {
                    renderer.handleEvent(window, *event);
                }
            }

            const sf::Time elapsed = clock.restart();
            ImGui::SFML::Update(window, elapsed);

            ImGui::SetNextWindowPos({10.f, 10.f}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({310.f, 420.f}, ImGuiCond_FirstUseEver);
            ImGui::Begin("Scenarios");
            if (ImGui::Button("Refresh")) {
                scenarioFiles = scanScenarioFiles(scenariosDir);
            }
            ImGui::SameLine();
            if (ImGui::Button("Restart")) {
                restartScenario(scenarioPath);
            }
            ImGui::SameLine();
            if (ImGui::Button(playing ? "Pause" : "Play")) {
                playing = !playing;
                accumulator = 0.0;
            }
            ImGui::SliderInt("FPS", &simulationFps, 1, 512);
            ImGui::Checkbox("Auto cam", &autoCamera);
            ImGui::Checkbox("Auto next", &autoAdvance);
            ImGui::Checkbox("Draw Terrain", &drawGameTerrain);
            ImGui::Checkbox("Collision boxes", &showCollisionBoxes);
            ImGui::Separator();
            ImGui::BeginChild("ScenarioList", {0.f, 210.f}, true);
            for (const std::filesystem::path& candidate : scenarioFiles) {
                const std::string label = scenarioLabel(candidate);
                if (ImGui::Selectable(label.c_str(), samePath(candidate, scenarioPath))) {
                    restartScenario(candidate);
                }
            }
            ImGui::EndChild();
            ImGui::Text("Frame %d (%s)", state.frame(), playing ? "playing" : "paused");
            ImGui::Text("Map: %s", state.mapPath().filename().string().c_str());
            ImGui::Text("Script: %s", state.scriptName().c_str());
            if (!scenarioError.empty()) {
                ImGui::Separator();
                ImGui::TextColored({1.f, 0.35f, 0.25f, 1.f}, "%s", scenarioError.c_str());
            }
            ImGui::End();

            if (playing) {
                const double simulationFrameSeconds = 1.0 / static_cast<double>(std::max(1, simulationFps));
                accumulator += elapsed.asSeconds();
                while (accumulator >= simulationFrameSeconds) {
                    if (!state.isTerminal()) {
                        state.update();
                    }
                    accumulator -= simulationFrameSeconds;
                }
            } else {
                accumulator = 0.0;
            }

            if (autoCamera) {
                renderer.fitToUnits(window, state);
            }

            if (autoAdvance && state.isTerminal()) {
                if (const auto next = nextScenarioPath()) {
                    if (!restartScenario(*next)) {
                        autoAdvance = false;
                    }
                }
            }

            if (state.frame() / 24 != lastPrintedFrame / 24) {
                printSummary(state);
                lastPrintedFrame = state.frame();
            }

            renderer.draw(window, state, showCollisionBoxes, drawGameTerrain);
            ImGui::SFML::Render(window);
            window.display();
        }
        ImGui::SFML::Shutdown(window);
    } catch (const std::exception& e) {
        std::cerr << "StarcraftCombatSim error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
