#define WLR_USE_UNSTABLE

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <any>
#include <chrono>
#include <ranges>
#include <unistd.h>

#include "Wobbly.hpp"
#include "globals.hpp"
#include "shaders.hpp"

using Render::GL::g_pHyprOpenGL;

typedef std::string (*origStyleValid)(CHyprAnimationManager*, const std::string&, const std::string&);

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static bool isWobblyStyle(const std::string& config, std::string style) {
    std::ranges::transform(style, style.begin(), [](unsigned char c) { return std::tolower(c); });
    return config == "windowsMove" && (style == "wobbly" || style.starts_with("wobbly "));
}

static std::string hkStyleValidInConfigVar(CHyprAnimationManager* thisptr, const std::string& config, const std::string& style) {
    if (isWobblyStyle(config, style))
        return "";

    return (*(origStyleValid)g_pStyleValidHook->m_original)(thisptr, config, style);
}

static bool hasWobbly(PHLWINDOW pWindow) {
    return std::ranges::any_of(pWindow->m_transformers, [pWindow](const auto& transformer) {
        const auto WOBBLY = dynamic_cast<CWobblyTransformer*>(transformer.get());
        return WOBBLY && WOBBLY->belongsTo(pWindow);
    });
}

static void addWobbly(PHLWINDOW pWindow) {
    if (!pWindow || hasWobbly(pWindow))
        return;

    pWindow->m_transformers.push_back(makeUnique<CWobblyTransformer>(pWindow));
}

static int onTick(void*) {
    static auto LAST = std::chrono::steady_clock::now();
    const auto  NOW  = std::chrono::steady_clock::now();
    const float DT   = std::clamp(std::chrono::duration<float>(NOW - LAST).count(), 0.001F, 0.05F);
    LAST             = NOW;

    const auto TRANSFORMERS = g_transformers;
    for (auto* const transformer : TRANSFORMERS) {
        if (transformer)
            transformer->tick(DT);
    }

    const int TIMEOUT = g_pHyprRenderer->m_mostHzMonitor ? 1000.0 / g_pHyprRenderer->m_mostHzMonitor->m_refreshRate : 16;
    wl_event_source_timer_update(g_pTick, TIMEOUT);
    return 0;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprwobbly] Failure in initialization: Version mismatch (headers ver is not equal to running Hyprland ver)",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprwobbly] Version mismatch");
    }

    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:enabled", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:mode", Hyprlang::STRING{"always"});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:grid_width", Hyprlang::INT{4});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:grid_height", Hyprlang::INT{4});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:tiles_x", Hyprlang::INT{12});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:tiles_y", Hyprlang::INT{12});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:spring_k", Hyprlang::FLOAT{18.F});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:friction", Hyprlang::FLOAT{8.F});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:mass", Hyprlang::FLOAT{12.F});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:move_factor", Hyprlang::FLOAT{0.65F});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:resize_factor", Hyprlang::FLOAT{0.45F});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprwobbly:max_warp", Hyprlang::FLOAT{140.F});

    auto FNS = HyprlandAPI::findFunctionsByName(PHANDLE, "styleValidInConfigVar");
    for (auto& fn : FNS) {
        if (!fn.demangled.contains("CHyprAnimationManager"))
            continue;

        g_pStyleValidHook = HyprlandAPI::createFunctionHook(PHANDLE, fn.address, (void*)::hkStyleValidInConfigVar);
        break;
    }

    if (!g_pStyleValidHook || !g_pStyleValidHook->hook()) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprwobbly] Failure in initialization: failed to hook animation style validator", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprwobbly] failed to hook animation style validator");
    }

    g_pHyprOpenGL->makeEGLCurrent();
    g_pWobblyShader = makeShared<CShader>();
    g_shaderReady   = g_pWobblyShader->createProgram(WOBBLY_VERTEX_SHADER, WOBBLY_FRAGMENT_SHADER);
    if (!g_shaderReady) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprwobbly] Failure in initialization: shader compilation failed", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprwobbly] shader compilation failed");
    }

    g_openWindowListener = Event::bus()->m_events.window.open.listen([](PHLWINDOW pWindow) { addWobbly(pWindow); });

    g_pTick = wl_event_loop_add_timer(g_pCompositor->m_wlEventLoop, &onTick, nullptr);
    wl_event_source_timer_update(g_pTick, 1);

    for (auto& window : g_pCompositor->m_windows) {
        if (window->isHidden() || !window->m_isMapped)
            continue;

        addWobbly(window);
    }

    HyprlandAPI::reloadConfig();
    HyprlandAPI::addNotification(PHANDLE, "[hyprwobbly] Initialized successfully!", CHyprColor{0.2, 1.0, 0.2, 1.0}, 5000);

    return {"hyprwobbly", "Compiz-style wobbly windows for Hyprland", "Ivan Malison", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (g_pTick) {
        wl_event_source_remove(g_pTick);
        g_pTick = nullptr;
    }
    g_openWindowListener.reset();

    for (auto& window : g_pCompositor->m_windows) {
        std::erase_if(window->m_transformers, [](const auto& transformer) { return dynamic_cast<CWobblyTransformer*>(transformer.get()) != nullptr; });
    }

    g_transformers.clear();
    if (g_pWobblyShader) {
        g_pWobblyShader->destroy();
        g_pWobblyShader.reset();
    }
    g_shaderReady = false;
}
