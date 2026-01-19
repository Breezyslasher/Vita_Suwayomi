/**
 * RotatableImage - Image view with rotation support
 * Extends brls::Image to add rotation capability (0, 90, 180, 270 degrees)
 */

#pragma once

#include <borealis.hpp>

namespace vitasuwayomi {

class RotatableImage : public brls::Image {
public:
    RotatableImage();
    ~RotatableImage() = default;

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
     * Cycle through rotation values (0 -> 90 -> 180 -> 270 -> 0)
     */
    void cycleRotation();

    /**
     * Set background color (shown in margins when image doesn't fill view)
     */
    void setBackgroundFillColor(NVGcolor color) { m_bgColor = color; }

    static brls::View* create();

private:
    float m_rotationDegrees = 0.0f;
    float m_rotationRadians = 0.0f;
    NVGcolor m_bgColor = nvgRGBA(26, 26, 46, 255);  // Default dark mode color
};

} // namespace vitasuwayomi
