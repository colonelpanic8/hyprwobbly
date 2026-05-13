#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Transformer.hpp>

#include <chrono>
#include <vector>

class CWobblyTransformer : public IWindowTransformer {
  public:
    explicit CWobblyTransformer(PHLWINDOW pWindow);
    ~CWobblyTransformer() override;

    void                     preWindowRender(CSurfacePassElement::SRenderData* pRenderData) override;
    SP<Render::IFramebuffer> transform(SP<Render::IFramebuffer> in) override;

    void                     tick(float dt);
    void                     damage();
    bool                     belongsTo(PHLWINDOW pWindow) const;

  private:
    struct SPoint {
        Vector2D pos;
        Vector2D rest;
        Vector2D velocity;
        Vector2D force;
    };

    struct SSpring {
        size_t   a = 0;
        size_t   b = 0;
        Vector2D offset;
    };

    WP<Desktop::View::CWindow> m_window;

    std::vector<SPoint>        m_points;
    std::vector<SSpring>       m_springs;

    SP<Render::IFramebuffer>   m_outputFB;
    Vector2D                   m_sizePx;
    CBox                       m_sourceBoxLayout;
    CBox                       m_sourceBoxPx;
    CBox                       m_lastDamage;
    bool                       m_initialized = false;
    bool                       m_active      = false;
    bool                       m_wasDragging = false;

    int                        gridWidth() const;
    int                        gridHeight() const;
    int                        tileCountX() const;
    int                        tileCountY() const;
    float                      springK() const;
    float                      friction() const;
    float                      mass() const;
    float                      moveFactor() const;
    float                      resizeFactor() const;
    float                      maxWarp() const;
    bool                       enabled() const;
    std::string                mode() const;
    bool                       hasWobblyAnimationStyle() const;
    bool                       shouldWobble() const;

    size_t                     index(int x, int y) const;
    void                       resetModel(const Vector2D& size);
    void                       rebuildSprings();
    void                       applyMoveImpulse(const Vector2D& delta, const Vector2D& anchor);
    void                       applyResizeImpulse(const Vector2D& deltaSize);
    void                       stepSimulation(float dt);
    void                       constrainWarp();
    bool                       isSettled() const;
    Vector2D                   sample(float u, float v) const;
    CBox                       deformedBoundsLayout() const;
    Vector2D                   currentPointerLocalPx(const CBox& boxLayout) const;
};
