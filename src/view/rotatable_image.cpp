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
    if (m_rotationDegrees == 0.0f) {
        // No rotation, draw normally
        brls::Image::draw(vg, x, y, width, height, style, ctx);
        return;
    }

    // Calculate center of the view
    float centerX = x + width / 2.0f;
    float centerY = y + height / 2.0f;

    // Save NanoVG state
    nvgSave(vg);

    // Move to center, rotate, then move back
    nvgTranslate(vg, centerX, centerY);
    nvgRotate(vg, m_rotationRadians);

    // For 90 and 270 degree rotations, we need to swap width/height
    float drawWidth = width;
    float drawHeight = height;
    if (m_rotationDegrees == 90.0f || m_rotationDegrees == 270.0f) {
        // Swap dimensions for proper aspect ratio
        drawWidth = height;
        drawHeight = width;
    }

    // Draw the image centered at origin (which is now at the view's center after translation)
    brls::Image::draw(vg, -drawWidth / 2.0f, -drawHeight / 2.0f, drawWidth, drawHeight, style, ctx);

    // Restore NanoVG state
    nvgRestore(vg);
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
