/**
 * RotatableImage - Custom image view with rotation support and no edge artifacts
 * Uses direct NanoVG rendering to avoid borealis Image edge clamping issues
 */

#pragma once

#include <borealis.hpp>
#include <string>

namespace vitasuwayomi {

class RotatableImage : public brls::Box {
public:
    RotatableImage();
    ~RotatableImage();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    /**
     * Set image from memory buffer (TGA, PNG, JPG data)
     */
    void setImageFromMem(const unsigned char* data, size_t size);

    /**
     * Set image from file path
     */
    void setImageFromFile(const std::string& path);

    /**
     * Clear the current image
     */
    void clearImage();

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
     * Set scaling type (FIT maintains aspect ratio)
     */
    void setScalingType(brls::ImageScalingType type) { m_scalingType = type; }

    /**
     * Set background color (shown in margins when image doesn't fill view)
     */
    void setBackgroundFillColor(NVGcolor color) { m_bgColor = color; }

    /**
     * Get image dimensions
     */
    int getImageWidth() const { return m_imageWidth; }
    int getImageHeight() const { return m_imageHeight; }

    /**
     * Check if image is loaded
     */
    bool hasImage() const { return m_nvgImage != 0; }

    /**
     * Set zoom level (1.0 = normal, >1.0 = zoomed in)
     */
    void setZoomLevel(float level);

    /**
     * Get current zoom level
     */
    float getZoomLevel() const { return m_zoomLevel; }

    /**
     * Set zoom offset (pan position when zoomed)
     */
    void setZoomOffset(brls::Point offset);

    /**
     * Get current zoom offset
     */
    brls::Point getZoomOffset() const { return m_zoomOffset; }

    /**
     * Reset zoom to normal (1.0x with no offset)
     */
    void resetZoom();

    static brls::View* create();

private:
    int m_nvgImage = 0;           // NanoVG image handle
    int m_imageWidth = 0;
    int m_imageHeight = 0;
    float m_rotationDegrees = 0.0f;
    float m_rotationRadians = 0.0f;
    brls::ImageScalingType m_scalingType = brls::ImageScalingType::FIT;
    NVGcolor m_bgColor = nvgRGBA(26, 26, 46, 255);  // Default dark mode color

    // Zoom state
    float m_zoomLevel = 1.0f;      // Current zoom level (1.0 = normal)
    brls::Point m_zoomOffset = {0, 0};  // Pan offset when zoomed

    // Calculate image bounds for current scaling type
    void calculateImageBounds(float viewX, float viewY, float viewW, float viewH,
                              float& imgX, float& imgY, float& imgW, float& imgH);
};

} // namespace vitasuwayomi
