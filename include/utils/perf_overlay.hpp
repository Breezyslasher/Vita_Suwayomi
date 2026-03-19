/**
 * VitaSuwayomi - Performance Debug Overlay
 * Lightweight FPS/frame-time overlay for profiling rendering bottlenecks.
 * Shows: FPS, frame time, texture upload queue depth, draw call estimate,
 * and per-section timing breakdown.
 */

#pragma once

#include <borealis.hpp>
#include <chrono>
#include <string>
#include <cstdio>

namespace vitasuwayomi {

class PerfOverlay {
public:
    static PerfOverlay& getInstance();

    // Call at the very START of the frame (before any drawing)
    void beginFrame();

    // Call at the very END of the frame (after all drawing)
    void endFrame();

    // Mark a named section start/end for breakdown timing
    void beginSection(const char* name);
    void endSection(const char* name);

    // Record texture upload count this frame
    void recordTextureUploads(int count);

    // Record pending texture queue size
    void recordPendingTextures(int count);

    // Draw the overlay (call at end of frame, before endFrame)
    void draw(NVGcontext* vg, float screenWidth, float screenHeight);

    // Enable/disable
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // Flush and close log file
    void closeLog();

private:
    PerfOverlay() = default;
    ~PerfOverlay();

    bool m_enabled = false;

    // Log file
    FILE* m_logFile = nullptr;
    int m_logInterval = 60;   // Write to log every N frames (once per second at 60fps)
    int m_logCounter = 0;
    void openLogIfNeeded();
    void writeLogEntry();

    // Frame timing
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint m_frameStart;
    TimePoint m_lastFpsUpdate;
    int m_frameCount = 0;
    float m_fps = 0.0f;
    float m_frameTimeMs = 0.0f;
    float m_maxFrameTimeMs = 0.0f;  // Worst frame in last second

    // Rolling history for graph
    static constexpr int HISTORY_SIZE = 60;
    float m_frameTimeHistory[HISTORY_SIZE] = {};
    int m_historyIndex = 0;

    // Section timing (up to 8 named sections)
    static constexpr int MAX_SECTIONS = 8;
    struct Section {
        const char* name = nullptr;
        TimePoint start;
        float lastMs = 0.0f;
    };
    Section m_sections[MAX_SECTIONS];
    int m_sectionCount = 0;

    // GPU/texture stats
    int m_textureUploadsThisFrame = 0;
    int m_pendingTextures = 0;

    // Helpers
    int findSection(const char* name);
};

// Convenience macros for easy profiling
#define PERF_BEGIN_FRAME() PerfOverlay::getInstance().beginFrame()
#define PERF_END_FRAME()   PerfOverlay::getInstance().endFrame()
#define PERF_BEGIN(name)   PerfOverlay::getInstance().beginSection(name)
#define PERF_END(name)     PerfOverlay::getInstance().endSection(name)
#define PERF_DRAW(vg, w, h) PerfOverlay::getInstance().draw(vg, w, h)

} // namespace vitasuwayomi
