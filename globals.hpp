#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Shader.hpp>

#include <vector>

class CWobblyTransformer;

inline HANDLE                           PHANDLE = nullptr;
inline SP<HOOK_CALLBACK_FN>             g_pOpenWindowHook;
inline CFunctionHook*                   g_pStyleValidHook = nullptr;
inline wl_event_source*                 g_pTick           = nullptr;
inline SShader                          g_wobblyShader;
inline bool                             g_shaderReady = false;
inline GLuint                           g_wobblyVAO   = 0;
inline GLuint                           g_wobblyVBO   = 0;
inline std::vector<CWobblyTransformer*> g_transformers;
