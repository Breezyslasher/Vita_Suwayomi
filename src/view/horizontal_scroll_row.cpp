/**
 * VitaSuwayomi - Horizontal Scroll Row implementation
 */

#include "view/horizontal_scroll_row.hpp"
#include "view/manga_item_cell.hpp"

namespace vitasuwayomi {

HorizontalScrollRow::HorizontalScrollRow() {
    this->setAxis(brls::Axis::ROW);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setClipsToBounds(true);

    // Add pan gesture for touch scrolling
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            onPan(status, soundToPlay);
        }, brls::PanAxis::HORIZONTAL));
}

void HorizontalScrollRow::draw(NVGcontext* vg, float x, float y, float width, float height,
                               brls::Style style, brls::FrameContext* ctx) {
    brls::Box::draw(vg, x, y, width, height, style, ctx);

    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, width, height);

    for (auto* child : this->getChildren()) {
        auto* cell = dynamic_cast<MangaItemCell*>(child);
        if (!cell) continue;

        float cx = cell->getDrawX();
        float cy = cell->getDrawY();
        float cw = cell->getDrawW();
        float ch = cell->getDrawH();
        if (cw <= 0 || ch <= 0) continue;

        int nvgImg = cell->getCoverImage();
        if (nvgImg != 0) {
            float imgW = static_cast<float>(cell->getCoverWidth());
            float imgH = static_cast<float>(cell->getCoverHeight());
            if (imgW > 0 && imgH > 0) {
                float scaleW = cw / imgW, scaleH = ch / imgH;
                float scale = (scaleW > scaleH) ? scaleW : scaleH;
                float sw = imgW * scale;
                float sh = imgH * scale;
                float ox = cx + (cw - sw) * 0.5f;
                float oy = cy + (ch - sh) * 0.5f;

                NVGpaint paint = nvgImagePattern(vg, ox, oy, sw, sh, 0, nvgImg, 1.0f);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, cx, cy, cw, ch, 4.0f);
                nvgFillPaint(vg, paint);
                nvgFill(vg);
            }
        } else {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx, cy, cw, ch, 4.0f);
            nvgFillColor(vg, nvgRGB(40, 40, 48));
            nvgFill(vg);
        }
    }

    nvgRestore(vg);
}

void HorizontalScrollRow::onLayout() {
    brls::Box::onLayout();

    // Calculate content width from children
    m_contentWidth = 0.0f;
    for (auto* child : this->getChildren()) {
        m_contentWidth += child->getWidth() + child->getMarginLeft() + child->getMarginRight();
    }

    m_visibleWidth = this->getWidth();

    // Ensure scroll offset is valid
    float maxOffset = std::max(0.0f, m_contentWidth - m_visibleWidth);
    m_scrollOffset = std::max(0.0f, std::min(m_scrollOffset, maxOffset));

    updateScroll();
}

void HorizontalScrollRow::onPan(brls::PanGestureStatus status, brls::Sound* sound) {
    if (status.state == brls::GestureState::START) {
        m_panStartOffset = m_scrollOffset;
    }
    else if (status.state == brls::GestureState::STAY || status.state == brls::GestureState::END) {
        // Calculate new offset based on pan delta
        float deltaX = status.position.x - status.startPosition.x;
        float newOffset = m_panStartOffset - deltaX;

        // Clamp to valid range
        float maxOffset = std::max(0.0f, m_contentWidth - m_visibleWidth);
        m_scrollOffset = std::max(0.0f, std::min(newOffset, maxOffset));

        updateScroll();
    }
}

void HorizontalScrollRow::updateScroll() {
    // Apply translation to all children
    for (auto* child : this->getChildren()) {
        child->setTranslationX(-m_scrollOffset);
    }
}

void HorizontalScrollRow::setScrollOffset(float offset) {
    float maxOffset = std::max(0.0f, m_contentWidth - m_visibleWidth);
    m_scrollOffset = std::max(0.0f, std::min(offset, maxOffset));
    updateScroll();
}

void HorizontalScrollRow::scrollToView(brls::View* targetView) {
    if (!targetView) return;

    // Find the view's position within the row
    float viewLeft = 0.0f;
    bool found = false;

    for (auto* child : this->getChildren()) {
        if (child == targetView) {
            found = true;
            break;
        }
        viewLeft += child->getWidth() + child->getMarginLeft() + child->getMarginRight();
    }

    if (!found) return;

    float viewRight = viewLeft + targetView->getWidth();

    // Check if view is outside visible area
    float visibleLeft = m_scrollOffset;
    float visibleRight = m_scrollOffset + m_visibleWidth;

    if (viewLeft < visibleLeft) {
        // Scroll left to show view
        setScrollOffset(viewLeft);
    }
    else if (viewRight > visibleRight) {
        // Scroll right to show view
        setScrollOffset(viewRight - m_visibleWidth);
    }
}

brls::View* HorizontalScrollRow::getNextFocus(brls::FocusDirection direction, brls::View* currentView) {
    // Get the default next focus
    brls::View* nextFocus = brls::Box::getNextFocus(direction, currentView);

    // If navigating left/right within this row, scroll to keep focused view visible
    if (nextFocus && (direction == brls::FocusDirection::LEFT || direction == brls::FocusDirection::RIGHT)) {
        // Schedule scroll after focus is applied
        brls::sync([this, nextFocus]() {
            scrollToView(nextFocus);
        });
    }

    return nextFocus;
}

} // namespace vitasuwayomi
