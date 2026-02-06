/**
 * VitaSuwayomi - History Tab implementation
 * Shows chronological reading history with date/time and quick resume
 */

#include "view/history_tab.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/async.hpp"
#include "utils/image_loader.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace vitasuwayomi {

HistoryTab::HistoryTab() {
    m_alive = std::make_shared<bool>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Header row with title and clear button
    auto* headerRow = new brls::Box();
    headerRow->setAxis(brls::Axis::ROW);
    headerRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    headerRow->setAlignItems(brls::AlignItems::CENTER);
    headerRow->setMarginBottom(15);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Reading History");
    m_titleLabel->setFontSize(28);
    headerRow->addView(m_titleLabel);

    // Clear history button
    auto* clearBtn = new brls::Button();
    clearBtn->setText("Clear");
    clearBtn->setWidth(80);
    clearBtn->setHeight(35);
    clearBtn->registerClickAction([this](brls::View* view) {
        clearHistory();
        return true;
    });
    headerRow->addView(clearBtn);

    this->addView(headerRow);

    // Scrollable content
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_contentBox = new brls::Box();
    m_contentBox->setAxis(brls::Axis::COLUMN);

    m_scrollView->setContentView(m_contentBox);
    this->addView(m_scrollView);

    // Empty state (hidden by default)
    m_emptyStateBox = new brls::Box();
    m_emptyStateBox->setAxis(brls::Axis::COLUMN);
    m_emptyStateBox->setJustifyContent(brls::JustifyContent::CENTER);
    m_emptyStateBox->setAlignItems(brls::AlignItems::CENTER);
    m_emptyStateBox->setGrow(1.0f);
    m_emptyStateBox->setVisibility(brls::Visibility::GONE);

    auto* emptyIcon = new brls::Label();
    emptyIcon->setText("No Reading History");
    emptyIcon->setFontSize(24);
    emptyIcon->setTextColor(nvgRGB(128, 128, 128));
    emptyIcon->setMarginBottom(10);
    m_emptyStateBox->addView(emptyIcon);

    auto* emptyHint = new brls::Label();
    emptyHint->setText("Start reading manga to see your history here");
    emptyHint->setFontSize(16);
    emptyHint->setTextColor(nvgRGB(100, 100, 100));
    m_emptyStateBox->addView(emptyHint);

    this->addView(m_emptyStateBox);

    // Register refresh action
    this->registerAction("Refresh", brls::ControllerButton::BUTTON_BACK, [this](brls::View*) {
        refresh();
        return true;
    });

    brls::Logger::debug("HistoryTab: Created");
}

HistoryTab::~HistoryTab() {
    if (m_alive) {
        *m_alive = false;
    }
    brls::Logger::debug("HistoryTab: Destroyed");
}

void HistoryTab::onFocusGained() {
    brls::Box::onFocusGained();
    if (!m_loaded) {
        loadHistory();
    }
}

void HistoryTab::refresh() {
    m_loaded = false;
    loadHistory();
}

void HistoryTab::loadHistory() {
    brls::Logger::debug("HistoryTab: Loading history...");
    m_titleLabel->setText("Reading History (Loading...)");

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<ReadingHistoryItem> history;

        // Fetch up to 100 recent history items
        bool success = client.fetchReadingHistory(0, 100, history);

        brls::sync([this, history, success, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            m_historyItems = history;
            m_loaded = true;

            // Update UI
            m_contentBox->clearViews();

            if (m_historyItems.empty()) {
                m_scrollView->setVisibility(brls::Visibility::GONE);
                m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
                m_titleLabel->setText("Reading History");
            } else {
                m_scrollView->setVisibility(brls::Visibility::VISIBLE);
                m_emptyStateBox->setVisibility(brls::Visibility::GONE);
                m_titleLabel->setText("Reading History (" + std::to_string(m_historyItems.size()) + ")");

                // Group by date
                std::string lastDate;
                for (size_t i = 0; i < m_historyItems.size(); i++) {
                    const auto& item = m_historyItems[i];

                    // Date header
                    std::string dateStr = formatTimestamp(item.lastReadAt);
                    if (dateStr != lastDate) {
                        lastDate = dateStr;

                        auto* dateHeader = new brls::Label();
                        dateHeader->setText(dateStr);
                        dateHeader->setFontSize(16);
                        dateHeader->setTextColor(nvgRGB(0, 150, 136));  // Teal
                        dateHeader->setMarginTop(i > 0 ? 20 : 5);
                        dateHeader->setMarginBottom(8);
                        m_contentBox->addView(dateHeader);
                    }

                    // History item row
                    auto* itemRow = new brls::Box();
                    itemRow->setAxis(brls::Axis::ROW);
                    itemRow->setAlignItems(brls::AlignItems::CENTER);
                    itemRow->setPadding(10);
                    itemRow->setMarginBottom(6);
                    itemRow->setCornerRadius(8);
                    itemRow->setBackgroundColor(nvgRGBA(40, 40, 40, 255));
                    itemRow->setFocusable(true);
                    itemRow->setHeight(80);

                    // Cover image
                    auto* coverImage = new brls::Image();
                    coverImage->setWidth(50);
                    coverImage->setHeight(70);
                    coverImage->setCornerRadius(4);
                    coverImage->setScalingType(brls::ImageScalingType::FILL);
                    coverImage->setMarginRight(12);

                    // Load cover
                    if (!item.mangaThumbnail.empty()) {
                        std::string url = item.mangaThumbnail;
                        if (url[0] == '/') {
                            url = SuwayomiClient::getInstance().getServerUrl() + url;
                        }
                        ImageLoader::loadAsync(url, nullptr, coverImage);
                    }
                    itemRow->addView(coverImage);

                    // Info column
                    auto* infoBox = new brls::Box();
                    infoBox->setAxis(brls::Axis::COLUMN);
                    infoBox->setGrow(1.0f);
                    infoBox->setJustifyContent(brls::JustifyContent::CENTER);

                    // Manga title
                    auto* titleLabel = new brls::Label();
                    std::string title = item.mangaTitle;
                    if (title.length() > 35) {
                        title = title.substr(0, 33) + "..";
                    }
                    titleLabel->setText(title);
                    titleLabel->setFontSize(15);
                    titleLabel->setTextColor(nvgRGB(255, 255, 255));
                    infoBox->addView(titleLabel);

                    // Chapter info
                    auto* chapterLabel = new brls::Label();
                    std::string chapterText = item.chapterName;
                    if (chapterText.empty()) {
                        chapterText = "Chapter " + std::to_string(static_cast<int>(item.chapterNumber));
                    }
                    chapterLabel->setText(chapterText);
                    chapterLabel->setFontSize(13);
                    chapterLabel->setTextColor(nvgRGB(180, 180, 180));
                    infoBox->addView(chapterLabel);

                    // Progress and time
                    auto* progressLabel = new brls::Label();
                    std::string progressText = "Page " + std::to_string(item.lastPageRead + 1);
                    if (item.pageCount > 0) {
                        progressText += "/" + std::to_string(item.pageCount);
                    }
                    progressText += " - " + formatRelativeTime(item.lastReadAt);
                    progressLabel->setText(progressText);
                    progressLabel->setFontSize(11);
                    progressLabel->setTextColor(nvgRGB(120, 120, 120));
                    infoBox->addView(progressLabel);

                    itemRow->addView(infoBox);

                    // Resume button/indicator
                    auto* resumeLabel = new brls::Label();
                    resumeLabel->setText(">");
                    resumeLabel->setFontSize(20);
                    resumeLabel->setTextColor(nvgRGB(0, 150, 136));
                    resumeLabel->setMarginLeft(10);
                    itemRow->addView(resumeLabel);

                    // Click to resume reading
                    int index = static_cast<int>(i);
                    ReadingHistoryItem capturedItem = item;
                    itemRow->registerClickAction([this, capturedItem](brls::View* view) {
                        onHistoryItemSelected(capturedItem);
                        return true;
                    });
                    itemRow->addGestureRecognizer(new brls::TapGestureRecognizer(itemRow));

                    // Start button for menu
                    itemRow->registerAction("Menu", brls::ControllerButton::BUTTON_START, [this, capturedItem, index](brls::View*) {
                        showHistoryItemMenu(capturedItem, index);
                        return true;
                    });

                    m_contentBox->addView(itemRow);
                }
            }

            brls::Logger::info("HistoryTab: Loaded {} history items", m_historyItems.size());
        });
    });
}

void HistoryTab::onHistoryItemSelected(const ReadingHistoryItem& item) {
    brls::Logger::info("HistoryTab: Resume reading '{}' chapter {} at page {}",
                       item.mangaTitle, item.chapterNumber, item.lastPageRead);

    // Resume reading from last page
    Application::getInstance().pushReaderActivityAtPage(
        item.mangaId,
        static_cast<int>(item.chapterNumber),  // Using chapter number as index
        item.lastPageRead,
        item.mangaTitle
    );
}

void HistoryTab::showHistoryItemMenu(const ReadingHistoryItem& item, int index) {
    std::vector<std::string> options = {
        "Continue Reading",
        "View Manga Details",
        "Remove from History"
    };

    auto* dropdown = new brls::Dropdown(
        item.mangaTitle,
        options,
        [this, item](int selected) {
            if (selected < 0) return;

            brls::sync([this, item, selected]() {
                switch (selected) {
                    case 0:  // Continue Reading
                        onHistoryItemSelected(item);
                        break;
                    case 1:  // View Manga Details
                        Application::getInstance().pushMangaDetailView(item.mangaId);
                        break;
                    case 2:  // Remove from History
                        removeHistoryItem(item);
                        break;
                }
            });
        }
    );
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void HistoryTab::clearHistory() {
    brls::Dialog* dialog = new brls::Dialog("Clear all reading history?");

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Clear", [dialog, this]() {
        dialog->close();
        // Note: Suwayomi API doesn't have a clear history endpoint
        // We just clear our local view
        m_historyItems.clear();
        m_contentBox->clearViews();
        m_scrollView->setVisibility(brls::Visibility::GONE);
        m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
        m_titleLabel->setText("Reading History");
        brls::Application::notify("History cleared");
    });

    dialog->open();
}

void HistoryTab::removeHistoryItem(const ReadingHistoryItem& item) {
    // Note: Suwayomi API doesn't have a remove history item endpoint
    // We just remove from our local view
    for (auto it = m_historyItems.begin(); it != m_historyItems.end(); ++it) {
        if (it->chapterId == item.chapterId && it->mangaId == item.mangaId) {
            m_historyItems.erase(it);
            break;
        }
    }

    // Refresh display
    m_loaded = false;
    m_contentBox->clearViews();

    // Rebuild the list
    if (m_historyItems.empty()) {
        m_scrollView->setVisibility(brls::Visibility::GONE);
        m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
        m_titleLabel->setText("Reading History");
    } else {
        // Reload to rebuild UI
        loadHistory();
    }

    brls::Application::notify("Removed from history");
}

std::string HistoryTab::formatTimestamp(int64_t timestamp) {
    if (timestamp <= 0) return "Unknown date";

    time_t time = static_cast<time_t>(timestamp / 1000);  // Convert from milliseconds
    struct tm* tm_info = localtime(&time);

    if (!tm_info) return "Unknown date";

    // Get current date
    time_t now = std::time(nullptr);
    struct tm* now_tm = localtime(&now);

    // Check if it's today
    if (tm_info->tm_year == now_tm->tm_year &&
        tm_info->tm_yday == now_tm->tm_yday) {
        return "Today";
    }

    // Check if it's yesterday
    if (tm_info->tm_year == now_tm->tm_year &&
        tm_info->tm_yday == now_tm->tm_yday - 1) {
        return "Yesterday";
    }

    // Check if it's this week
    int daysDiff = now_tm->tm_yday - tm_info->tm_yday;
    if (tm_info->tm_year == now_tm->tm_year && daysDiff >= 0 && daysDiff < 7) {
        const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        return days[tm_info->tm_wday];
    }

    // Format as date
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%b %d, %Y", tm_info);
    return std::string(buffer);
}

std::string HistoryTab::formatRelativeTime(int64_t timestamp) {
    if (timestamp <= 0) return "";

    time_t time = static_cast<time_t>(timestamp / 1000);
    time_t now = std::time(nullptr);
    int64_t diff = now - time;

    if (diff < 60) {
        return "just now";
    } else if (diff < 3600) {
        int minutes = static_cast<int>(diff / 60);
        return std::to_string(minutes) + (minutes == 1 ? " min ago" : " mins ago");
    } else if (diff < 86400) {
        int hours = static_cast<int>(diff / 3600);
        return std::to_string(hours) + (hours == 1 ? " hour ago" : " hours ago");
    } else if (diff < 604800) {
        int days = static_cast<int>(diff / 86400);
        return std::to_string(days) + (days == 1 ? " day ago" : " days ago");
    } else {
        // Format time
        struct tm* tm_info = localtime(&time);
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%H:%M", tm_info);
        return std::string(buffer);
    }
}

} // namespace vitasuwayomi
