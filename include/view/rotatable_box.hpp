/**
 * RotatableBox - Box with rotation support for display at 0/90/180/270 degrees
 * Applies NanoVG rotation transform to all children during draw
 */

#pragma once

#include <borealis.hpp>

namespace vitasuwayomi {

class RotatableBox : public brls::Box {
public:
    RotatableBox();
    ~RotatableBox() = default;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    /**
     * Set rotation in degrees (0, 90, 180, 270)
     */
    void setRotation(float degrees);

    /**
     * Get current rotation in degrees
     */
    float getRotation() const { return m_rotationDegrees; }

    /**
     * Set slide offset for swipe carousel (NanoVG-based positioning)
     */
    void setSlideOffset(float x, float y);
    void resetSlideOffset();

    /**
     * Set a custom background color drawn via NanoVG inside draw().
     * This replaces the borealis backgroundColor XML attribute, which is
     * drawn at the view's layout position BEFORE draw() is called and
     * therefore ignores slide offsets. By drawing the background manually
     * inside draw(), it correctly follows the slide offset transform.
     */
    void setCustomBackground(NVGcolor color);

    static brls::View* create();

private:
    float m_rotationDegrees = 0.0f;
    float m_slideX = 0.0f;
    float m_slideY = 0.0f;

    // Custom NanoVG background (drawn inside draw() so it follows slide offset)
    bool m_hasCustomBg = false;
    NVGcolor m_customBgColor = {};
};

} // namespace vitasuwayomi
