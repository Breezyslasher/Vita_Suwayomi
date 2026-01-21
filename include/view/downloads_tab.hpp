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

    brls::Box* m_queueContainer = nullptr;
    brls::Label* m_queueEmptyLabel = nullptr;
    brls::Box* m_localContainer = nullptr;
    brls::Label* m_localEmptyLabel = nullptr;
};

} // namespace vitasuwayomi
