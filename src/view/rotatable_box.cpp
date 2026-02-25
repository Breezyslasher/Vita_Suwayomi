/**
 * RotatableBox implementation
 * Applies NanoVG rotation transform around center before drawing children
 */

#include "view/rotatable_box.hpp"

#ifndef NVG_PI
#define NVG_PI 3.14159265358979323846264338327f
#endif

namespace vitasuwayomi {

RotatableBox::RotatableBox() {
    this->setFocusable(false);
}

void RotatableBox::setRotation(float degrees) {
    // Normalize to 0, 90, 180, 270
    int normalized = static_cast<int>(degrees) % 360;
    if (normalized < 0) normalized += 360;

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

void RotatableBox::setSlideOffset(float x, float y) {
    m_slideX = x;
    m_slideY = y;
}

void RotatableBox::resetSlideOffset() {
    m_slideX = 0.0f;
    m_slideY = 0.0f;
}

void RotatableBox::draw(NVGcontext* vg, float x, float y, float width, float height,
                         brls::Style style, brls::FrameContext* ctx) {
    // Apply slide offset for swipe carousel if active
    bool hasSlide = (m_slideX != 0.0f || m_slideY != 0.0f);
    if (hasSlide) {
        nvgSave(vg);
        nvgIntersectScissor(vg, x, y, width, height);
        nvgTranslate(vg, m_slideX, m_slideY);
    }

    if (m_rotationDegrees == 0.0f) {
        Box::draw(vg, x, y, width, height, style, ctx);
    } else {
        nvgSave(vg);

        float centerX = x + width / 2.0f;
        float centerY = y + height / 2.0f;
        nvgTranslate(vg, centerX, centerY);
        nvgRotate(vg, m_rotationDegrees * NVG_PI / 180.0f);
        nvgTranslate(vg, -centerX, -centerY);

        Box::draw(vg, x, y, width, height, style, ctx);

        nvgRestore(vg);
    }

    if (hasSlide) {
        nvgRestore(vg);
    }
}

brls::View* RotatableBox::create() {
    return new RotatableBox();
}

} // namespace vitasuwayomi
