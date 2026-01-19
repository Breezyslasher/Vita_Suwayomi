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
    // STEP 1: Always draw background first to fill the entire area
    // This ensures margins have the correct color even before image loads
    nvgSave(vg);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, m_bgColor);
    nvgFill(vg);
    nvgRestore(vg);

    // STEP 2: Get image dimensions to calculate actual rendered bounds
    int imgW = 0, imgH = 0;
    NVGLUimage* texture = this->getTexture();
    if (texture) {
        nvgluImageSize(texture, &imgW, &imgH);
    }

    // If no image loaded yet, just show background
    if (imgW <= 0 || imgH <= 0) {
        return;
    }

    // STEP 3: Calculate the actual image bounds for FIT scaling
    float imgX = x, imgY = y, imgWidth = width, imgHeight = height;
    float viewAspect = width / height;
    float imgAspect = (float)imgW / (float)imgH;

    if (viewAspect > imgAspect) {
        // View is wider than image - image fills height, has margins on sides
        imgHeight = height;
        imgWidth = height * imgAspect;
        imgX = x + (width - imgWidth) / 2.0f;
    } else {
        // View is taller than image - image fills width, has margins on top/bottom
        imgWidth = width;
        imgHeight = width / imgAspect;
        imgY = y + (height - imgHeight) / 2.0f;
    }

    // STEP 4: Use scissor to clip rendering to calculated image bounds
    // This prevents any edge sampling artifacts from bleeding into margins
    // Add small inset (1 pixel) to ensure we clip any edge interpolation artifacts
    const float EDGE_INSET = 1.0f;
    float scissorX = imgX + EDGE_INSET;
    float scissorY = imgY + EDGE_INSET;
    float scissorW = imgWidth - (2.0f * EDGE_INSET);
    float scissorH = imgHeight - (2.0f * EDGE_INSET);

    // Only apply inset if image is large enough (inset shouldn't take more than 5%)
    if (imgWidth < 40.0f || imgHeight < 40.0f) {
        // Image too small for inset, use original bounds
        scissorX = imgX;
        scissorY = imgY;
        scissorW = imgWidth;
        scissorH = imgHeight;
    }

    nvgSave(vg);
    nvgScissor(vg, scissorX, scissorY, scissorW, scissorH);

    // STEP 5: Draw the image with rotation if needed
    if (m_rotationDegrees == 0.0f) {
        brls::Image::draw(vg, x, y, width, height, style, ctx);
    } else {
        float centerX = x + width / 2.0f;
        float centerY = y + height / 2.0f;
        nvgTranslate(vg, centerX, centerY);
        nvgRotate(vg, m_rotationRadians);
        nvgTranslate(vg, -centerX, -centerY);
        brls::Image::draw(vg, x, y, width, height, style, ctx);
    }

    nvgRestore(vg);  // Restores scissor and any transforms
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
