/**
 * VitaSuwayomi - Performance Debug Overlay implementation
 */

#include "utils/perf_overlay.hpp"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace vitasuwayomi {

static const char* PERF_LOG_PATH = "ux0:data/VitaSuwayomi/perf.log";

PerfOverlay& PerfOverlay::getInstance() {
    static PerfOverlay instance;
    return instance;
}

PerfOverlay::~PerfOverlay() {
    closeLog();
}

void PerfOverlay::closeLog() {
    if (m_logFile) {
        fclose(m_logFile);
        m_logFile = nullptr;
    }
}

void PerfOverlay::openLogIfNeeded() {
    if (m_logFile) return;
    m_logFile = fopen(PERF_LOG_PATH, "w");
    if (m_logFile) {
        fprintf(m_logFile, "=== VitaSuwayomi Perf Log ===\n");
        fprintf(m_logFile, "FPS | Frame(ms) | Max(ms) | TexUp | Pending | Sections...\n");
        fprintf(m_logFile, "-----------------------------------------------------------\n");
        fflush(m_logFile);
    }
}

void PerfOverlay::writeLogEntry() {
    if (!m_logFile) return;

    fprintf(m_logFile, "FPS:%.0f Frame:%.1fms Max:%.1fms TexUp:%d Pend:%d",
            m_fps, m_frameTimeMs, m_maxFrameTimeMs,
            m_textureUploadsThisFrame, m_pendingTextures);

    for (int i = 0; i < m_sectionCount; i++) {
        fprintf(m_logFile, " | %s:%.1fms", m_sections[i].name, m_sections[i].lastMs);
    }
    fprintf(m_logFile, "\n");
    fflush(m_logFile);
}

void PerfOverlay::beginFrame() {
    if (!m_enabled) return;
    m_frameStart = Clock::now();
    m_textureUploadsThisFrame = 0;
}

void PerfOverlay::endFrame() {
    if (!m_enabled) return;

    auto now = Clock::now();
    float frameMs = std::chrono::duration<float, std::milli>(now - m_frameStart).count();
    m_frameTimeMs = frameMs;

    // Record to history
    m_frameTimeHistory[m_historyIndex] = frameMs;
    m_historyIndex = (m_historyIndex + 1) % HISTORY_SIZE;

    // Track worst frame
    if (frameMs > m_maxFrameTimeMs) {
        m_maxFrameTimeMs = frameMs;
    }

    // Update FPS every second
    m_frameCount++;
    float elapsed = std::chrono::duration<float>(now - m_lastFpsUpdate).count();
    if (elapsed >= 1.0f) {
        m_fps = m_frameCount / elapsed;
        m_frameCount = 0;
        m_lastFpsUpdate = now;
        m_maxFrameTimeMs = 0.0f;  // Reset worst frame tracking each second
    }

    // Write to log file periodically (every m_logInterval frames)
    m_logCounter++;
    if (m_logCounter >= m_logInterval) {
        m_logCounter = 0;
        openLogIfNeeded();
        writeLogEntry();
    }
}

void PerfOverlay::beginSection(const char* name) {
    if (!m_enabled) return;
    int idx = findSection(name);
    if (idx < 0) {
        if (m_sectionCount >= MAX_SECTIONS) return;
        idx = m_sectionCount++;
        m_sections[idx].name = name;
    }
    m_sections[idx].start = Clock::now();
}

void PerfOverlay::endSection(const char* name) {
    if (!m_enabled) return;
    int idx = findSection(name);
    if (idx < 0) return;
    auto now = Clock::now();
    m_sections[idx].lastMs = std::chrono::duration<float, std::milli>(now - m_sections[idx].start).count();
}

void PerfOverlay::recordTextureUploads(int count) {
    m_textureUploadsThisFrame += count;
}

void PerfOverlay::recordPendingTextures(int count) {
    m_pendingTextures = count;
}

int PerfOverlay::findSection(const char* name) {
    for (int i = 0; i < m_sectionCount; i++) {
        if (m_sections[i].name == name) return i;  // Pointer comparison (same literal)
    }
    return -1;
}

void PerfOverlay::draw(NVGcontext* vg, float screenWidth, float screenHeight) {
    if (!m_enabled) return;

    // Overlay position: top-right corner
    float panelW = 220.0f;
    float lineH = 14.0f;
    int numLines = 4 + m_sectionCount;  // FPS, frame time, textures, pending + sections
    float graphH = 40.0f;
    float panelH = (numLines * lineH) + graphH + 16.0f;
    float panelX = screenWidth - panelW - 4.0f;
    float panelY = 4.0f;

    // Semi-transparent background
    nvgBeginPath(vg);
    nvgRoundedRect(vg, panelX, panelY, panelW, panelH, 4.0f);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 180));
    nvgFill(vg);

    // Text setup
    nvgFontSize(vg, 12.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    float textX = panelX + 6.0f;
    float textY = panelY + 4.0f;
    char buf[128];

    // FPS - color coded
    NVGcolor fpsColor;
    if (m_fps >= 50.0f) fpsColor = nvgRGB(0, 255, 0);        // Green: good
    else if (m_fps >= 30.0f) fpsColor = nvgRGB(255, 255, 0);  // Yellow: ok
    else if (m_fps >= 15.0f) fpsColor = nvgRGB(255, 128, 0);  // Orange: bad
    else fpsColor = nvgRGB(255, 0, 0);                         // Red: terrible

    snprintf(buf, sizeof(buf), "FPS: %.0f", m_fps);
    nvgFillColor(vg, fpsColor);
    nvgText(vg, textX, textY, buf, nullptr);
    textY += lineH;

    // Frame time
    snprintf(buf, sizeof(buf), "Frame: %.1fms (max %.1fms)", m_frameTimeMs, m_maxFrameTimeMs);
    nvgFillColor(vg, nvgRGB(220, 220, 220));
    nvgText(vg, textX, textY, buf, nullptr);
    textY += lineH;

    // Texture upload stats
    snprintf(buf, sizeof(buf), "Tex uploads: %d  pending: %d", m_textureUploadsThisFrame, m_pendingTextures);
    NVGcolor texColor = m_pendingTextures > 10 ? nvgRGB(255, 128, 0) : nvgRGB(180, 180, 180);
    nvgFillColor(vg, texColor);
    nvgText(vg, textX, textY, buf, nullptr);
    textY += lineH;

    // Target line label
    snprintf(buf, sizeof(buf), "Target: 16.7ms (60fps)");
    nvgFillColor(vg, nvgRGB(120, 120, 120));
    nvgText(vg, textX, textY, buf, nullptr);
    textY += lineH;

    // Section breakdown
    for (int i = 0; i < m_sectionCount; i++) {
        snprintf(buf, sizeof(buf), "  %s: %.1fms", m_sections[i].name, m_sections[i].lastMs);
        // Color code: red if section takes more than 8ms
        NVGcolor secColor = m_sections[i].lastMs > 8.0f ? nvgRGB(255, 100, 100) : nvgRGB(160, 160, 160);
        nvgFillColor(vg, secColor);
        nvgText(vg, textX, textY, buf, nullptr);
        textY += lineH;
    }

    // Frame time graph
    float graphX = panelX + 6.0f;
    float graphY = textY + 4.0f;
    float graphW = panelW - 12.0f;
    float maxGraphMs = 50.0f;  // Graph scales to 50ms max

    // Graph background
    nvgBeginPath(vg);
    nvgRect(vg, graphX, graphY, graphW, graphH);
    nvgFillColor(vg, nvgRGBA(20, 20, 20, 200));
    nvgFill(vg);

    // 16.7ms target line (60fps)
    float targetY = graphY + graphH - (16.7f / maxGraphMs) * graphH;
    nvgBeginPath(vg);
    nvgMoveTo(vg, graphX, targetY);
    nvgLineTo(vg, graphX + graphW, targetY);
    nvgStrokeColor(vg, nvgRGBA(0, 255, 0, 80));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    // 33.3ms line (30fps)
    float target30Y = graphY + graphH - (33.3f / maxGraphMs) * graphH;
    nvgBeginPath(vg);
    nvgMoveTo(vg, graphX, target30Y);
    nvgLineTo(vg, graphX + graphW, target30Y);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 0, 60));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    // Frame time bars
    float barW = graphW / HISTORY_SIZE;
    for (int i = 0; i < HISTORY_SIZE; i++) {
        int idx = (m_historyIndex + i) % HISTORY_SIZE;
        float ms = m_frameTimeHistory[idx];
        if (ms <= 0.0f) continue;

        float barH = std::min((ms / maxGraphMs) * graphH, graphH);
        float barX = graphX + i * barW;
        float barY = graphY + graphH - barH;

        NVGcolor barColor;
        if (ms <= 16.7f) barColor = nvgRGBA(0, 200, 0, 200);       // Under 60fps budget
        else if (ms <= 33.3f) barColor = nvgRGBA(255, 200, 0, 200); // Under 30fps budget
        else barColor = nvgRGBA(255, 50, 50, 200);                   // Over 30fps budget

        nvgBeginPath(vg);
        nvgRect(vg, barX, barY, barW - 0.5f, barH);
        nvgFillColor(vg, barColor);
        nvgFill(vg);
    }
}

} // namespace vitasuwayomi
