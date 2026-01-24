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

private:
    void refresh();
    void refreshQueue();
    void refreshLocalDownloads();
    void showDownloadOptions(const std::string& ratingKey, const std::string& title);

    // Queue section (server downloads)
    brls::Box* m_queueSection = nullptr;
    brls::Label* m_queueHeader = nullptr;
    brls::ScrollingFrame* m_queueScroll = nullptr;
    brls::Box* m_queueContainer = nullptr;
    brls::Label* m_queueEmptyLabel = nullptr;

    // Local downloads section
    brls::Box* m_localSection = nullptr;
    brls::Label* m_localHeader = nullptr;
    brls::ScrollingFrame* m_localScroll = nullptr;
    brls::Box* m_localContainer = nullptr;
    brls::Label* m_localEmptyLabel = nullptr;
};

} // namespace vitasuwayomi
