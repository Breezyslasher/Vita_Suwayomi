/**
 * RotatableLabel implementation
 * Uses NanoVG for rotated text rendering
 */

#include "view/rotatable_label.hpp"
#include <cmath>

#ifndef NVG_PI
#define NVG_PI 3.14159265358979323846264338327f
#endif

namespace vitasuwayomi {

RotatableLabel::RotatableLabel() {
    this->setFocusable(false);
}

void RotatableLabel::setText(const std::string& text) {
    m_text = text;
    this->invalidate();
}

void RotatableLabel::setRotation(float degrees) {
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

    this->invalidate();
}

void RotatableLabel::draw(NVGcontext* vg, float x, float y, float width, float height,
                           brls::Style style, brls::FrameContext* ctx) {
    if (m_text.empty()) return;

    // Calculate text bounds to determine actual size needed
    nvgFontSize(vg, m_fontSize);
    nvgFontFace(vg, "regular");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    float bounds[4];
    nvgTextBounds(vg, 0, 0, m_text.c_str(), nullptr, bounds);
    float textWidth = bounds[2] - bounds[0];
    float textHeight = bounds[3] - bounds[1];

    // Box dimensions with padding
    float boxWidth = textWidth + m_paddingH * 2;
    float boxHeight = textHeight + m_paddingV * 2;

    // For 90/270 rotation, swap effective dimensions for positioning
    bool isRotated90or270 = (m_rotationDegrees == 90.0f || m_rotationDegrees == 270.0f);
    float effectiveBoxWidth = isRotated90or270 ? boxHeight : boxWidth;
    float effectiveBoxHeight = isRotated90or270 ? boxWidth : boxHeight;

    // Center of the view
    float viewCenterX = x + width / 2.0f;
    float viewCenterY = y + height / 2.0f;

    // Save NVG state
    nvgSave(vg);

    // Apply rotation around the view center
    nvgTranslate(vg, viewCenterX, viewCenterY);
    nvgRotate(vg, m_rotationDegrees * NVG_PI / 180.0f);
    nvgTranslate(vg, -viewCenterX, -viewCenterY);

    // Draw background box (centered in the view)
    float boxX = viewCenterX - boxWidth / 2.0f;
    float boxY = viewCenterY - boxHeight / 2.0f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, boxX, boxY, boxWidth, boxHeight, m_cornerRadius);
    nvgFillColor(vg, m_bgColor);
    nvgFill(vg);

    // Draw text
    nvgFillColor(vg, m_textColor);
    nvgText(vg, viewCenterX, viewCenterY, m_text.c_str(), nullptr);

    // Restore NVG state
    nvgRestore(vg);
}

brls::View* RotatableLabel::create() {
    return new RotatableLabel();
}

} // namespace vitasuwayomi
