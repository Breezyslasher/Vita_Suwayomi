/**
 * RotatableImage - Custom image view with rotation support and no edge artifacts
 * Uses direct NanoVG rendering to avoid borealis Image edge clamping issues
 */

#pragma once

#include <borealis.hpp>
#include <string>

namespace vitasuwayomi {

// Custom scale modes for the reader (more granular than borealis)
enum class ImageScaleMode {
    FIT_SCREEN,     // Fit entire image on screen, maintain aspect ratio
    FIT_WIDTH,      // Fit width to screen, may crop top/bottom
    FIT_HEIGHT,     // Fit height to screen, may crop left/right
    ORIGINAL        // Show at native 1:1 pixel resolution, centered
};

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
     * Set scaling type (legacy borealis compat - maps to custom mode)
     */
    void setScalingType(brls::ImageScalingType type) {
        switch (type) {
            case brls::ImageScalingType::FIT: m_scaleMode = ImageScaleMode::FIT_SCREEN; break;
            case brls::ImageScalingType::FILL: m_scaleMode = ImageScaleMode::FIT_WIDTH; break;
            default: m_scaleMode = ImageScaleMode::FIT_SCREEN; break;
        }
    }

    /**
     * Set custom scale mode (preferred over setScalingType)
     */
    void setScaleMode(ImageScaleMode mode) { m_scaleMode = mode; }

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

    /**
     * Transfer the NVG image handle from another RotatableImage to this one.
     * The source image is left empty (handle moved, not copied).
     * Used to instantly swap the preview page into the main page after a swipe.
     */
    void takeImageFrom(RotatableImage* other);

    static brls::View* create();

private:
    int m_nvgImage = 0;           // NanoVG image handle
    int m_imageWidth = 0;
    int m_imageHeight = 0;
    float m_rotationDegrees = 0.0f;
    float m_rotationRadians = 0.0f;
    ImageScaleMode m_scaleMode = ImageScaleMode::FIT_SCREEN;
    NVGcolor m_bgColor = nvgRGBA(26, 26, 46, 255);  // Default dark mode color

    // Zoom state
    float m_zoomLevel = 1.0f;      // Current zoom level (1.0 = normal)
    brls::Point m_zoomOffset = {0, 0};  // Pan offset when zoomed

    // Calculate image bounds for current scaling type
    void calculateImageBounds(float viewX, float viewY, float viewW, float viewH,
                              float& imgX, float& imgY, float& imgW, float& imgH);
};

} // namespace vitasuwayomi
