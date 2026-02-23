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

void RotatableBox::draw(NVGcontext* vg, float x, float y, float width, float height,
                         brls::Style style, brls::FrameContext* ctx) {
    if (m_rotationDegrees == 0.0f) {
        // No rotation - draw normally
        Box::draw(vg, x, y, width, height, style, ctx);
        return;
    }

    float centerX = x + width / 2.0f;
    float centerY = y + height / 2.0f;

    nvgSave(vg);
    nvgTranslate(vg, centerX, centerY);
    nvgRotate(vg, m_rotationDegrees * NVG_PI / 180.0f);
    nvgTranslate(vg, -centerX, -centerY);

    Box::draw(vg, x, y, width, height, style, ctx);

    nvgRestore(vg);
}

brls::View* RotatableBox::create() {
    return new RotatableBox();
}

} // namespace vitasuwayomi
