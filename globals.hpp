#pragma once

#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Shader.hpp>

#include <vector>

class CWobblyTransformer;

inline HANDLE                           PHANDLE = nullptr;
inline CHyprSignalListener              g_openWindowListener;
inline CFunctionHook*                   g_pStyleValidHook = nullptr;
inline wl_event_source*                 g_pTick           = nullptr;
inline SP<CShader>                      g_pWobblyShader;
inline bool                             g_shaderReady = false;
inline std::vector<CWobblyTransformer*> g_transformers;
