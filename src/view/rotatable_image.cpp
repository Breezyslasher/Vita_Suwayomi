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

    float imageAspect = (float)m_imageWidth / (float)m_imageHeight;
    float viewAspect = viewW / viewH;

    switch (m_scalingType) {
        case brls::ImageScalingType::FIT:
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

        case brls::ImageScalingType::FILL:
            // Fill view, cropping if necessary
            if (viewAspect > imageAspect) {
                // View is wider - fill width, crop top/bottom
                imgW = viewW;
                imgH = viewW / imageAspect;
                imgX = viewX;
                imgY = viewY + (viewH - imgH) / 2.0f;
            } else {
                // View is taller - fill height, crop left/right
                imgH = viewH;
                imgW = viewH * imageAspect;
                imgX = viewX + (viewW - imgW) / 2.0f;
                imgY = viewY;
            }
            break;

        case brls::ImageScalingType::STRETCH:
        default:
            // Stretch to fill (distorts aspect ratio)
            imgX = viewX;
            imgY = viewY;
            imgW = viewW;
            imgH = viewH;
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

    // Calculate where the image should be drawn
    float imgX, imgY, imgW, imgH;
    calculateImageBounds(x, y, width, height, imgX, imgY, imgW, imgH);

    // Save state
    nvgSave(vg);

    // Apply rotation if needed
    if (m_rotationDegrees != 0.0f) {
        float centerX = x + width / 2.0f;
        float centerY = y + height / 2.0f;
        nvgTranslate(vg, centerX, centerY);
        nvgRotate(vg, m_rotationRadians);
        nvgTranslate(vg, -centerX, -centerY);
    }

    // CRITICAL: Use scissor to clip rendering to ONLY the image bounds
    // This prevents any edge artifacts from appearing in the margins
    nvgScissor(vg, imgX, imgY, imgW, imgH);

    // Create image pattern - sized exactly to the image bounds
    // The pattern maps the full texture (0,0 to imageWidth,imageHeight) to the draw bounds
    NVGpaint imgPaint = nvgImagePattern(vg, imgX, imgY, imgW, imgH, 0, m_nvgImage, 1.0f);

    // Draw the image as a filled rectangle
    nvgBeginPath(vg);
    nvgRect(vg, imgX, imgY, imgW, imgH);
    nvgFillPaint(vg, imgPaint);
    nvgFill(vg);

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

brls::View* RotatableImage::create() {
    return new RotatableImage();
}

} // namespace vitasuwayomi
