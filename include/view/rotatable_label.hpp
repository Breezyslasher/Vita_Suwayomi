/**
 * RotatableLabel - Label with rotation support for display at 0/90/180/270 degrees
 * Uses NanoVG text rendering with rotation transforms
 */

#pragma once

#include <borealis.hpp>
#include <string>

namespace vitasuwayomi {

class RotatableLabel : public brls::Box {
public:
    RotatableLabel();
    ~RotatableLabel() = default;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    /**
     * Set the text to display
     */
    void setText(const std::string& text);

    /**
     * Get current text
     */
    const std::string& getText() const { return m_text; }

    /**
     * Set rotation in degrees (0, 90, 180, 270)
     */
    void setRotation(float degrees);

    /**
     * Get current rotation in degrees
     */
    float getRotation() const { return m_rotationDegrees; }

    /**
     * Set text color
     */
    void setTextColor(NVGcolor color) { m_textColor = color; }

    /**
     * Set font size
     */
    void setFontSize(float size) { m_fontSize = size; }

    /**
     * Set background color (for the rounded rect behind text)
     */
    void setBackgroundColor(NVGcolor color) { m_bgColor = color; }

    /**
     * Set corner radius
     */
    void setCornerRadius(float radius) { m_cornerRadius = radius; }

    /**
     * Set padding
     */
    void setPadding(float horizontal, float vertical) {
        m_paddingH = horizontal;
        m_paddingV = vertical;
    }

    static brls::View* create();

private:
    std::string m_text = "";
    float m_rotationDegrees = 0.0f;
    float m_fontSize = 14.0f;
    float m_cornerRadius = 4.0f;
    float m_paddingH = 12.0f;
    float m_paddingV = 6.0f;
    NVGcolor m_textColor = nvgRGBA(255, 255, 255, 255);
    NVGcolor m_bgColor = nvgRGBA(0, 0, 0, 170);  // Semi-transparent black
};

} // namespace vitasuwayomi
