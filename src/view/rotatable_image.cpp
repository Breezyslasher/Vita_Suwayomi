/**
 * RotatableImage implementation
 * Uses direct NanoVG rendering to avoid edge clamping artifacts
 */

#include "view/rotatable_image.hpp"
#include <cmath>

#ifndef NVG_PI
#define NVG_PI 3.14159265358979323846264338327f
#endif

namespace vitasuwayomi {

RotatableImage::RotatableImage() {
    // Set default properties
    this->setFocusable(false);
}

RotatableImage::~RotatableImage() {
    clearImage();
}

void RotatableImage::clearImage() {
    if (m_nvgImage != 0) {
        NVGcontext* vg = brls::Application::getNVGContext();
        if (vg) {
            nvgDeleteImage(vg, m_nvgImage);
        }
        m_nvgImage = 0;
        m_imageWidth = 0;
        m_imageHeight = 0;
    }
}

void RotatableImage::setImageFromMem(const unsigned char* data, size_t size) {
    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg || !data || size == 0) {
        return;
    }

    // Clear existing image
    clearImage();

    // Create NVG image from memory
    m_nvgImage = nvgCreateImageMem(vg, 0, const_cast<unsigned char*>(data), size);

    if (m_nvgImage != 0) {
        // Get image dimensions
        nvgImageSize(vg, m_nvgImage, &m_imageWidth, &m_imageHeight);
        brls::Logger::debug("RotatableImage: Loaded image {}x{}", m_imageWidth, m_imageHeight);
    } else {
        brls::Logger::error("RotatableImage: Failed to create NVG image");
    }

    this->invalidate();
}

void RotatableImage::setImageFromFile(const std::string& path) {
    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg || path.empty()) {
        return;
    }

    // Clear existing image
    clearImage();

    // Create NVG image from file
    m_nvgImage = nvgCreateImage(vg, path.c_str(), 0);

    if (m_nvgImage != 0) {
        nvgImageSize(vg, m_nvgImage, &m_imageWidth, &m_imageHeight);
        brls::Logger::debug("RotatableImage: Loaded image from file {}x{}", m_imageWidth, m_imageHeight);
    } else {
        brls::Logger::error("RotatableImage: Failed to load image from {}", path);
    }

    this->invalidate();
}

void RotatableImage::calculateImageBounds(float viewX, float viewY, float viewW, float viewH,
                                          float& imgX, float& imgY, float& imgW, float& imgH) {
    if (m_imageWidth <= 0 || m_imageHeight <= 0) {
        imgX = viewX;
        imgY = viewY;
        imgW = viewW;
        imgH = viewH;
        return;
    }

    // For 90/270 degree rotation, the effective dimensions are swapped
    bool isRotated90or270 = (m_rotationDegrees == 90.0f || m_rotationDegrees == 270.0f);
    int effectiveWidth = isRotated90or270 ? m_imageHeight : m_imageWidth;
    int effectiveHeight = isRotated90or270 ? m_imageWidth : m_imageHeight;

    float imageAspect = (float)effectiveWidth / (float)effectiveHeight;
    float viewAspect = viewW / viewH;

    switch (m_scaleMode) {
        case ImageScaleMode::FIT_SCREEN:
            // Fit entire image within view, maintaining aspect ratio
            if (viewAspect > imageAspect) {
                // View is wider - fit to height
                imgH = viewH;
                imgW = viewH * imageAspect;
                imgX = viewX + (viewW - imgW) / 2.0f;
                imgY = viewY;
            } else {
                // View is taller - fit to width
                imgW = viewW;
                imgH = viewW / imageAspect;
                imgX = viewX;
                imgY = viewY + (viewH - imgH) / 2.0f;
            }
            break;

        case ImageScaleMode::FIT_WIDTH:
            // Fit width to screen width, scale height proportionally
            imgW = viewW;
            imgH = viewW / imageAspect;
            imgX = viewX;
            imgY = viewY + (viewH - imgH) / 2.0f;
            break;

        case ImageScaleMode::FIT_HEIGHT:
            // Fit height to screen height, scale width proportionally
            imgH = viewH;
            imgW = viewH * imageAspect;
            imgX = viewX + (viewW - imgW) / 2.0f;
            imgY = viewY;
            break;

        case ImageScaleMode::ORIGINAL:
        default:
            // Show at native 1:1 pixel resolution, centered in view
            imgW = (float)effectiveWidth;
            imgH = (float)effectiveHeight;
            imgX = viewX + (viewW - imgW) / 2.0f;
            imgY = viewY + (viewH - imgH) / 2.0f;
            break;
    }
}

void RotatableImage::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* ctx) {
    // First draw the background to fill margins
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, m_bgColor);
    nvgFill(vg);

    // If no image, just show background
    if (m_nvgImage == 0 || m_imageWidth <= 0 || m_imageHeight <= 0) {
        return;
    }

    // Calculate where the image should be drawn (accounts for rotation)
    float imgX, imgY, imgW, imgH;
    calculateImageBounds(x, y, width, height, imgX, imgY, imgW, imgH);

    // Save state
    nvgSave(vg);

    // Use scissor to clip rendering to the view bounds (not calculated bounds when zoomed)
    nvgScissor(vg, x, y, width, height);

    // Calculate center of the destination area
    float centerX = imgX + imgW / 2.0f;
    float centerY = imgY + imgH / 2.0f;

    // Apply zoom transform (scale + translate)
    if (m_zoomLevel != 1.0f) {
        // Translate to center, apply zoom, then apply pan offset
        nvgTranslate(vg, centerX, centerY);
        nvgScale(vg, m_zoomLevel, m_zoomLevel);
        nvgTranslate(vg, m_zoomOffset.x, m_zoomOffset.y);
        nvgTranslate(vg, -centerX, -centerY);
    }

    // For rotated images, we need to render differently
    bool isRotated90or270 = (m_rotationDegrees == 90.0f || m_rotationDegrees == 270.0f);

    if (isRotated90or270) {
        // For 90/270 rotation, we need to swap the pattern dimensions
        // The image pattern needs to be sized for the rotated output
        nvgTranslate(vg, centerX, centerY);
        nvgRotate(vg, m_rotationRadians);
        nvgTranslate(vg, -centerX, -centerY);

        // When rotated 90/270, swap width/height for the pattern
        // The pattern should map the original texture to a rect that when rotated fills imgW x imgH
        float patternW = imgH;  // Swapped
        float patternH = imgW;  // Swapped
        float patternX = centerX - patternW / 2.0f;
        float patternY = centerY - patternH / 2.0f;

        NVGpaint imgPaint = nvgImagePattern(vg, patternX, patternY, patternW, patternH, 0, m_nvgImage, 1.0f);

        nvgBeginPath(vg);
        nvgRect(vg, patternX, patternY, patternW, patternH);
        nvgFillPaint(vg, imgPaint);
        nvgFill(vg);
    } else if (m_rotationDegrees == 180.0f) {
        // 180 degree rotation - same dimensions, just rotated
        nvgTranslate(vg, centerX, centerY);
        nvgRotate(vg, m_rotationRadians);
        nvgTranslate(vg, -centerX, -centerY);

        NVGpaint imgPaint = nvgImagePattern(vg, imgX, imgY, imgW, imgH, 0, m_nvgImage, 1.0f);

        nvgBeginPath(vg);
        nvgRect(vg, imgX, imgY, imgW, imgH);
        nvgFillPaint(vg, imgPaint);
        nvgFill(vg);
    } else {
        // No rotation (0 degrees)
        NVGpaint imgPaint = nvgImagePattern(vg, imgX, imgY, imgW, imgH, 0, m_nvgImage, 1.0f);

        nvgBeginPath(vg);
        nvgRect(vg, imgX, imgY, imgW, imgH);
        nvgFillPaint(vg, imgPaint);
        nvgFill(vg);
    }

    // Restore state (removes scissor and rotation)
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

    brls::Logger::debug("RotatableImage: setRotation({}) -> {} degrees", degrees, m_rotationDegrees);
    this->invalidate();
}

void RotatableImage::cycleRotation() {
    float newRotation = m_rotationDegrees + 90.0f;
    if (newRotation >= 360.0f) {
        newRotation = 0.0f;
    }
    setRotation(newRotation);
}

void RotatableImage::setZoomLevel(float level) {
    m_zoomLevel = std::max(0.5f, std::min(4.0f, level));  // Clamp between 0.5x and 4x
    this->invalidate();
}

void RotatableImage::setZoomOffset(brls::Point offset) {
    m_zoomOffset = offset;
    this->invalidate();
}

void RotatableImage::resetZoom() {
    m_zoomLevel = 1.0f;
    m_zoomOffset = {0, 0};
    this->invalidate();
}

brls::View* RotatableImage::create() {
    return new RotatableImage();
}

} // namespace vitasuwayomi
