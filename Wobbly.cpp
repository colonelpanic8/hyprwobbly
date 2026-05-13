#include "Wobbly.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/memory/Casts.hpp>

#include <algorithm>
#include <cmath>

#include "globals.hpp"

using namespace Hyprutils::Memory;
namespace {
    struct SMeshVertex {
        float x = 0;
        float y = 0;
        float u = 0;
        float v = 0;
    };

    template <typename T>
    T cfg(const std::string& name, T fallback) {
        const auto VALUE = HyprlandAPI::getConfigValue(PHANDLE, name);
        if (!VALUE)
            return fallback;

        if constexpr (std::is_same_v<T, int>) {
            auto* const* DATA = (Hyprlang::INT* const*)VALUE->getDataStaticPtr();
            return DATA && *DATA ? sc<int>(**DATA) : fallback;
        } else {
            auto* const* DATA = (Hyprlang::FLOAT* const*)VALUE->getDataStaticPtr();
            return DATA && *DATA ? sc<float>(**DATA) : fallback;
        }
    }

    float length(const Vector2D& vec) {
        return std::sqrt(vec.x * vec.x + vec.y * vec.y);
    }

    Vector2D lerp(const Vector2D& a, const Vector2D& b, float t) {
        return a + (b - a) * t;
    }
}

CWobblyTransformer::CWobblyTransformer(PHLWINDOW pWindow) : m_window(pWindow) {
    g_transformers.push_back(this);
}

CWobblyTransformer::~CWobblyTransformer() {
    damage();
    std::erase(g_transformers, this);
}

bool CWobblyTransformer::belongsTo(PHLWINDOW pWindow) const {
    return m_window.lock() == pWindow;
}

int CWobblyTransformer::gridWidth() const {
    return std::clamp(cfg<int>("plugin:hyprwobbly:grid_width", 4), 2, 16);
}

int CWobblyTransformer::gridHeight() const {
    return std::clamp(cfg<int>("plugin:hyprwobbly:grid_height", 4), 2, 16);
}

int CWobblyTransformer::tileCountX() const {
    return std::clamp(cfg<int>("plugin:hyprwobbly:tiles_x", 12), 1, 80);
}

int CWobblyTransformer::tileCountY() const {
    return std::clamp(cfg<int>("plugin:hyprwobbly:tiles_y", 12), 1, 80);
}

float CWobblyTransformer::springK() const {
    return std::clamp(cfg<float>("plugin:hyprwobbly:spring_k", 18.F), 0.1F, 200.F);
}

float CWobblyTransformer::friction() const {
    return std::clamp(cfg<float>("plugin:hyprwobbly:friction", 8.F), 0.F, 80.F);
}

float CWobblyTransformer::mass() const {
    return std::clamp(cfg<float>("plugin:hyprwobbly:mass", 12.F), 1.F, 200.F);
}

float CWobblyTransformer::moveFactor() const {
    return std::clamp(cfg<float>("plugin:hyprwobbly:move_factor", 0.65F), 0.F, 4.F);
}

float CWobblyTransformer::resizeFactor() const {
    return std::clamp(cfg<float>("plugin:hyprwobbly:resize_factor", 0.45F), 0.F, 4.F);
}

float CWobblyTransformer::maxWarp() const {
    return std::clamp(cfg<float>("plugin:hyprwobbly:max_warp", 140.F), 1.F, 600.F);
}

bool CWobblyTransformer::enabled() const {
    return cfg<int>("plugin:hyprwobbly:enabled", 1) != 0;
}

size_t CWobblyTransformer::index(int x, int y) const {
    return sc<size_t>(y * gridWidth() + x);
}

void CWobblyTransformer::resetModel(const Vector2D& size) {
    m_sizePx = {std::max(1.0, size.x), std::max(1.0, size.y)};
    m_points.clear();
    m_points.resize(sc<size_t>(gridWidth() * gridHeight()));

    for (int y = 0; y < gridHeight(); ++y) {
        for (int x = 0; x < gridWidth(); ++x) {
            const Vector2D rest   = {x * m_sizePx.x / (gridWidth() - 1), y * m_sizePx.y / (gridHeight() - 1)};
            m_points[index(x, y)] = {.pos = rest, .rest = rest};
        }
    }

    rebuildSprings();
}

void CWobblyTransformer::rebuildSprings() {
    m_springs.clear();

    for (int y = 0; y < gridHeight(); ++y) {
        for (int x = 0; x < gridWidth(); ++x) {
            if (x > 0)
                m_springs.push_back({index(x - 1, y), index(x, y), m_points[index(x, y)].rest - m_points[index(x - 1, y)].rest});
            if (y > 0)
                m_springs.push_back({index(x, y - 1), index(x, y), m_points[index(x, y)].rest - m_points[index(x, y - 1)].rest});
        }
    }
}

Vector2D CWobblyTransformer::currentPointerLocalPx(const CBox& boxLayout) const {
    const auto POINTER = g_pInputManager->getMouseCoordsInternal();
    return (POINTER - boxLayout.pos()) * (m_sourceBoxPx.w / std::max(1.0, boxLayout.w));
}

void CWobblyTransformer::applyMoveImpulse(const Vector2D& delta, const Vector2D& anchor) {
    const auto DIAG = std::max(1.F, sc<float>(length(m_sizePx)));

    for (auto& point : m_points) {
        const float dist   = sc<float>(length(point.rest - anchor));
        const float weight = std::clamp(dist / DIAG, 0.15F, 1.F);
        point.velocity -= delta * moveFactor() * weight;
    }

    m_active = true;
}

void CWobblyTransformer::applyResizeImpulse(const Vector2D& deltaSize) {
    const auto oldSize = m_sizePx;
    const auto oldPts  = m_points;

    resetModel(m_sizePx + deltaSize);

    if (oldPts.size() == m_points.size()) {
        for (size_t i = 0; i < m_points.size(); ++i)
            m_points[i].velocity = oldPts[i].velocity;
    }

    for (int y = 0; y < gridHeight(); ++y) {
        for (int x = 0; x < gridWidth(); ++x) {
            auto&       point = m_points[index(x, y)];
            const float wx    = sc<float>(x) / std::max(1, gridWidth() - 1);
            const float wy    = sc<float>(y) / std::max(1, gridHeight() - 1);
            point.velocity.x -= sc<float>(deltaSize.x) * resizeFactor() * wx;
            point.velocity.y -= sc<float>(deltaSize.y) * resizeFactor() * wy;
        }
    }

    if (oldSize != m_sizePx)
        m_active = true;
}

void CWobblyTransformer::preWindowRender(CSurfacePassElement::SRenderData* pRenderData) {
    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || !pRenderData || !pRenderData->pMonitor)
        return;

    const auto PMONITOR = pRenderData->pMonitor.lock();
    if (!PMONITOR)
        return;

    CBox fullBox = PWINDOW->getFullWindowBoundingBox();
    if (fullBox.empty())
        fullBox = {pRenderData->pos.x, pRenderData->pos.y, pRenderData->w, pRenderData->h};

    CBox fullBoxPx = fullBox.copy().translate(-PMONITOR->m_position).scale(PMONITOR->m_scale).round();

    if (fullBoxPx.w <= 1 || fullBoxPx.h <= 1)
        return;

    const Vector2D newSize = fullBoxPx.size();

    if (!m_initialized || m_points.size() != sc<size_t>(gridWidth() * gridHeight())) {
        m_sourceBoxLayout = fullBox;
        m_sourceBoxPx     = fullBoxPx;
        resetModel(newSize);
        m_initialized = true;
        return;
    }

    const Vector2D deltaPos  = fullBoxPx.pos() - m_sourceBoxPx.pos();
    const Vector2D deltaSize = newSize - m_sizePx;

    const auto     POINTER = g_pInputManager->getMouseCoordsInternal();
    m_wasDragging          = fullBox.containsPoint(POINTER);

    damage();

    if (std::abs(deltaSize.x) > 0.5 || std::abs(deltaSize.y) > 0.5)
        applyResizeImpulse(deltaSize);

    if (std::abs(deltaPos.x) > 0.5 || std::abs(deltaPos.y) > 0.5) {
        Vector2D anchor = m_sizePx / 2.0;
        if (m_wasDragging)
            anchor = currentPointerLocalPx(fullBox);

        anchor.x = std::clamp(anchor.x, 0.0, m_sizePx.x);
        anchor.y = std::clamp(anchor.y, 0.0, m_sizePx.y);
        applyMoveImpulse(deltaPos, anchor);
    }

    m_sourceBoxLayout = fullBox;
    m_sourceBoxPx     = fullBoxPx;
    damage();
}

void CWobblyTransformer::stepSimulation(float dt) {
    if (!m_active || m_points.empty())
        return;

    const int   SUBSTEPS = std::clamp(sc<int>(std::ceil(dt / 0.008F)), 1, 8);
    const float STEP     = dt / SUBSTEPS;
    const float K        = springK();
    const float FRICTION = friction();
    const float MASS     = mass();

    for (int substep = 0; substep < SUBSTEPS; ++substep) {
        for (auto& point : m_points)
            point.force = {};

        for (const auto& spring : m_springs) {
            auto&      a     = m_points[spring.a];
            auto&      b     = m_points[spring.b];
            const auto force = (b.pos - a.pos - spring.offset) * K;
            a.force += force;
            b.force -= force;
        }

        for (auto& point : m_points) {
            point.force -= point.velocity * FRICTION;
            point.force += (point.rest - point.pos) * (K * 0.18F);
            point.velocity += (point.force / MASS) * STEP;
            point.pos += point.velocity * STEP;
        }
    }

    constrainWarp();

    if (isSettled()) {
        for (auto& point : m_points) {
            point.pos      = point.rest;
            point.velocity = {};
        }
        m_active = false;
    }
}

void CWobblyTransformer::constrainWarp() {
    const auto LIMIT = maxWarp();
    for (auto& point : m_points) {
        auto diff = point.pos - point.rest;
        auto len  = length(diff);
        if (len > LIMIT)
            point.pos = point.rest + diff * (LIMIT / len);
    }
}

bool CWobblyTransformer::isSettled() const {
    for (const auto& point : m_points) {
        if (length(point.velocity) > 1.0)
            return false;
        if (length(point.pos - point.rest) > 0.75)
            return false;
    }
    return true;
}

Vector2D CWobblyTransformer::sample(float u, float v) const {
    if (m_points.empty())
        return {u * m_sizePx.x, v * m_sizePx.y};

    const float gx = std::clamp(u, 0.F, 1.F) * (gridWidth() - 1);
    const float gy = std::clamp(v, 0.F, 1.F) * (gridHeight() - 1);
    const int   x0 = std::clamp(sc<int>(std::floor(gx)), 0, gridWidth() - 1);
    const int   y0 = std::clamp(sc<int>(std::floor(gy)), 0, gridHeight() - 1);
    const int   x1 = std::min(x0 + 1, gridWidth() - 1);
    const int   y1 = std::min(y0 + 1, gridHeight() - 1);
    const float tx = gx - x0;
    const float ty = gy - y0;

    const auto  top    = lerp(m_points[index(x0, y0)].pos, m_points[index(x1, y0)].pos, tx);
    const auto  bottom = lerp(m_points[index(x0, y1)].pos, m_points[index(x1, y1)].pos, tx);
    return lerp(top, bottom, ty);
}

CBox CWobblyTransformer::deformedBoundsLayout() const {
    if (m_points.empty())
        return m_sourceBoxLayout;

    Vector2D min = m_points.front().pos;
    Vector2D max = m_points.front().pos;

    for (const auto& point : m_points) {
        min.x = std::min(min.x, point.pos.x);
        min.y = std::min(min.y, point.pos.y);
        max.x = std::max(max.x, point.pos.x);
        max.y = std::max(max.y, point.pos.y);
    }

    const double SCALE = std::max(0.01, m_sourceBoxPx.w / std::max(1.0, m_sourceBoxLayout.w));
    return {m_sourceBoxLayout.x + min.x / SCALE, m_sourceBoxLayout.y + min.y / SCALE, (max.x - min.x) / SCALE, (max.y - min.y) / SCALE};
}

void CWobblyTransformer::damage() {
    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || !PWINDOW->m_isMapped || PWINDOW->isHidden())
        return;

    const auto NOW = deformedBoundsLayout().expand(8);
    if (!NOW.empty())
        g_pHyprRenderer->damageBox(NOW);

    if (!m_lastDamage.empty())
        g_pHyprRenderer->damageBox(m_lastDamage);

    m_lastDamage = NOW;
}

void CWobblyTransformer::tick(float dt) {
    if (!enabled() || !m_initialized)
        return;

    if (!m_active)
        return;

    damage();
    stepSimulation(dt);
    damage();
}

CFramebuffer* CWobblyTransformer::transform(CFramebuffer* in) {
    if (!enabled() || !m_initialized || !m_active || !in || !in->getTexture() || !g_shaderReady)
        return in;

    const auto PMONITOR = g_pHyprOpenGL->m_renderData.pMonitor.lock();
    if (!PMONITOR)
        return in;

    const Vector2D MONSIZE = PMONITOR->m_transformedSize;

    if (!m_outputFB.isAllocated() || m_outputFB.m_size != MONSIZE)
        m_outputFB.alloc(MONSIZE.x, MONSIZE.y);

    m_outputFB.bind();
    g_pHyprOpenGL->m_renderData.currentFB = &m_outputFB;
    g_pHyprOpenGL->scissor(nullptr);
    glClearColor(0.F, 0.F, 0.F, 0.F);
    glClear(GL_COLOR_BUFFER_BIT);

    std::vector<SMeshVertex> vertices;
    vertices.reserve(sc<size_t>(tileCountX() * tileCountY() * 6));

    const auto pushVertex = [&](float u, float v) {
        const auto dst = m_sourceBoxPx.pos() + sample(u, v);
        const auto src = m_sourceBoxPx.pos() + Vector2D{u * m_sourceBoxPx.w, v * m_sourceBoxPx.h};
        vertices.push_back({
            .x = sc<float>(dst.x / MONSIZE.x),
            .y = sc<float>(dst.y / MONSIZE.y),
            .u = sc<float>(src.x / MONSIZE.x),
            .v = sc<float>(src.y / MONSIZE.y),
        });
    };

    for (int y = 0; y < tileCountY(); ++y) {
        const float v0 = sc<float>(y) / tileCountY();
        const float v1 = sc<float>(y + 1) / tileCountY();
        for (int x = 0; x < tileCountX(); ++x) {
            const float u0 = sc<float>(x) / tileCountX();
            const float u1 = sc<float>(x + 1) / tileCountX();

            pushVertex(u0, v0);
            pushVertex(u0, v1);
            pushVertex(u1, v0);

            pushVertex(u1, v0);
            pushVertex(u0, v1);
            pushVertex(u1, v1);
        }
    }

    glActiveTexture(GL_TEXTURE0);
    in->getTexture()->bind();
    in->getTexture()->setTexParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    in->getTexture()->setTexParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    g_pHyprOpenGL->useProgram(g_wobblyShader.program);
    g_wobblyShader.setUniformInt(SHADER_TEX, 0);

    CBox monbox   = {0, 0, MONSIZE.x, MONSIZE.y};
    auto matrix   = g_pHyprOpenGL->m_renderData.monitorProjection.projectBox(monbox, HYPRUTILS_TRANSFORM_NORMAL, monbox.rot);
    auto glMatrix = g_pHyprOpenGL->m_renderData.projection.copy().multiply(matrix);
    g_wobblyShader.setUniformMatrix3fv(SHADER_PROJ, 1, GL_TRUE, glMatrix.getMatrix());

    glBindVertexArray(g_wobblyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_wobblyVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(SMeshVertex), vertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(g_wobblyShader.uniformLocations[SHADER_POS_ATTRIB]);
    glEnableVertexAttribArray(g_wobblyShader.uniformLocations[SHADER_TEX_ATTRIB]);
    glVertexAttribPointer(g_wobblyShader.uniformLocations[SHADER_POS_ATTRIB], 2, GL_FLOAT, GL_FALSE, sizeof(SMeshVertex), (void*)offsetof(SMeshVertex, x));
    glVertexAttribPointer(g_wobblyShader.uniformLocations[SHADER_TEX_ATTRIB], 2, GL_FLOAT, GL_FALSE, sizeof(SMeshVertex), (void*)offsetof(SMeshVertex, u));
    glDrawArrays(GL_TRIANGLES, 0, vertices.size());
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    in->getTexture()->unbind();

    return &m_outputFB;
}
