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

    static brls::View* create();

private:
    float m_rotationDegrees = 0.0f;
};

} // namespace vitasuwayomi
