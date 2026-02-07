/**
 * VitaSuwayomi - Downloads Tab
 * View for managing offline downloads and showing download queue
 */

#pragma once

#include <borealis.hpp>

namespace vitasuwayomi {

class DownloadsTab : public brls::Box {
public:
    DownloadsTab();
    ~DownloadsTab() override = default;

    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;

private:
    void refresh();
    void refreshQueue();
    void refreshLocalDownloads();
    void showDownloadOptions(const std::string& ratingKey, const std::string& title);
    void startAutoRefresh();
    void stopAutoRefresh();
    void pauseAllDownloads();
    void clearAllDownloads();

    // Queue section (server downloads)
    brls::Box* m_queueSection = nullptr;
    brls::Label* m_queueHeader = nullptr;
    brls::ScrollingFrame* m_queueScroll = nullptr;
    brls::Box* m_queueContainer = nullptr;

    // Local downloads section
    brls::Box* m_localSection = nullptr;
    brls::Label* m_localHeader = nullptr;
    brls::ScrollingFrame* m_localScroll = nullptr;
    brls::Box* m_localContainer = nullptr;

    // Track currently focused X button icon (like book detail view)
    brls::Image* m_currentFocusedIcon = nullptr;

    // Top action icons
    brls::Box* m_actionsRow = nullptr;
    brls::Image* m_pauseIcon = nullptr;
    brls::Image* m_clearIcon = nullptr;

    // Start/Stop button
    brls::Button* m_startStopBtn = nullptr;
    brls::Label* m_startStopLabel = nullptr;

    // Download status display
    brls::Label* m_downloadStatusLabel = nullptr;
    bool m_downloaderRunning = false;

    // Auto-refresh state
    bool m_autoRefreshEnabled = false;
    bool m_autoRefreshTimerActive = false;

    // Swipe gesture state (avoid static variables)
    struct SwipeState {
        brls::Point touchStart;
        bool isValidSwipe = false;
    };
};

} // namespace vitasuwayomi
