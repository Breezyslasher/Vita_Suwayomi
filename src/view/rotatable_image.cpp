/**
 * RotatableImage implementation
 */

#include "view/rotatable_image.hpp"
#include <cmath>

#ifndef NVG_PI
#define NVG_PI 3.14159265358979323846264338327f
#endif

namespace vitasuwayomi {

RotatableImage::RotatableImage() {
    // Register as a Borealis view
}

void RotatableImage::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* ctx) {
    // STEP 1: Draw background first to fill the entire area
    // This ensures margins have the correct color before the image is drawn
    nvgSave(vg);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, m_bgColor);
    nvgFill(vg);
    nvgRestore(vg);

    // STEP 2: Draw the image with rotation if needed
    // The image has a 1-pixel transparent border added during loading to prevent
    // GPU edge clamping artifacts (edge pixels being stretched into margins)
    if (m_rotationDegrees == 0.0f) {
        // No rotation - draw normally
        brls::Image::draw(vg, x, y, width, height, style, ctx);
    } else {
        // Apply rotation around center
        float centerX = x + width / 2.0f;
        float centerY = y + height / 2.0f;

        nvgSave(vg);
        nvgTranslate(vg, centerX, centerY);
        nvgRotate(vg, m_rotationRadians);
        nvgTranslate(vg, -centerX, -centerY);
        brls::Image::draw(vg, x, y, width, height, style, ctx);
        nvgRestore(vg);
    }
}

void RotatableImage::setRotation(float degrees) {
    // Normalize to 0, 90, 180, 270
    int normalized = static_cast<int>(degrees) % 360;
    if (normalized < 0) normalized += 360;

    // Snap to nearest 90 degree increment
    if (normalized < 45) {
        m_rotationDegrees = 0.0f;
    } else if (normalized < 135) {
        m_rotationDegrees = 90.0f;
    } else if (normalized < 225) {
        m_rotationDegrees = 180.0f;
    } else if (normalized < 315) {
        m_rotationDegrees = 270.0f;
    } else {
        m_rotationDegrees = 0.0f;
    }

    // Convert to radians
    m_rotationRadians = m_rotationDegrees * NVG_PI / 180.0f;

    brls::Logger::info("RotatableImage: setRotation({}) -> {} degrees, {} radians",
                       degrees, m_rotationDegrees, m_rotationRadians);

    // Force redraw
    this->invalidate();
}

void RotatableImage::cycleRotation() {
    float newRotation = m_rotationDegrees + 90.0f;
    if (newRotation >= 360.0f) {
        newRotation = 0.0f;
    }
    setRotation(newRotation);
}

brls::View* RotatableImage::create() {
    return new RotatableImage();
}

} // namespace vitasuwayomi
