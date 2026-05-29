/**
 * VitaSuwayomi - Horizontal Scroll Row
 * A horizontal scrollable row for touch and d-pad navigation
 */

#pragma once

#include <borealis.hpp>

namespace vitasuwayomi {

class HorizontalScrollRow : public brls::Box {
public:
    HorizontalScrollRow();

    void onLayout() override;
    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override;

    // Set scroll offset
    void setScrollOffset(float offset);
    float getScrollOffset() const { return m_scrollOffset; }

    // Scroll to make a child view visible
    void scrollToView(brls::View* view);

private:
    void onPan(brls::PanGestureStatus status, brls::Sound* sound);
    void updateScroll();

    float m_scrollOffset = 0.0f;
    float m_contentWidth = 0.0f;
    float m_visibleWidth = 0.0f;
    float m_panStartOffset = 0.0f;
};

} // namespace vitasuwayomi
