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

    // Header row with title and buttons
    auto* headerRow = new brls::Box();
    headerRow->setAxis(brls::Axis::ROW);
    headerRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    headerRow->setAlignItems(brls::AlignItems::CENTER);
    headerRow->setMarginBottom(15);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Reading History");
    m_titleLabel->setFontSize(28);
    m_titleLabel->setGrow(1.0f);
    headerRow->addView(m_titleLabel);

    // Refresh button with triangle hint
    auto* refreshContainer = new brls::Box();
    refreshContainer->setAxis(brls::Axis::COLUMN);
    refreshContainer->setAlignItems(brls::AlignItems::CENTER);

    auto* triangleIcon = new brls::Image();
    triangleIcon->setWidth(16);
    triangleIcon->setHeight(16);
    triangleIcon->setScalingType(brls::ImageScalingType::FIT);
    triangleIcon->setImageFromFile("app0:resources/images/triangle_button.png");
    triangleIcon->setMarginBottom(2);
    refreshContainer->addView(triangleIcon);

    m_refreshBtn = new brls::Button();
    m_refreshBtn->setWidth(44);
    m_refreshBtn->setHeight(44);
    m_refreshBtn->setCornerRadius(8);
    m_refreshBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_refreshBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* refreshIcon = new brls::Image();
    refreshIcon->setWidth(24);
    refreshIcon->setHeight(24);
    refreshIcon->setScalingType(brls::ImageScalingType::FIT);
    refreshIcon->setImageFromFile("app0:resources/icons/refresh.png");
    m_refreshBtn->addView(refreshIcon);

    m_refreshBtn->registerClickAction([this](brls::View* view) {
        refresh();
        return true;
    });
    m_refreshBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_refreshBtn));
    refreshContainer->addView(m_refreshBtn);

    headerRow->addView(refreshContainer);
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

    // Loading indicator (centered, hidden by default)
    m_loadingLabel = new brls::Label();
    m_loadingLabel->setText("Loading history...");
    m_loadingLabel->setFontSize(18);
    m_loadingLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_loadingLabel->setTextColor(nvgRGB(150, 150, 150));
    m_loadingLabel->setMarginTop(50);
    m_loadingLabel->setVisibility(brls::Visibility::GONE);
    this->addView(m_loadingLabel);

    // Register refresh action (Triangle button)
    this->registerAction("Refresh", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        refresh();
        return true;
    });

    brls::Logger::debug("HistoryTab: Created");

    // Load history immediately on construction
    loadHistory();
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
    // Move focus to refresh button before clearing items to prevent crash
    // when focus is on a history item that will be destroyed
    if (m_refreshBtn) {
        brls::Application::giveFocus(m_refreshBtn);
    }

    m_loaded = false;
    m_currentOffset = 0;
    m_hasMoreItems = true;
    m_historyItems.clear();
    loadHistory();
}

void HistoryTab::loadHistory() {
    brls::Logger::debug("HistoryTab: Loading history...");

    // Show loading indicator
    m_loadingLabel->setVisibility(brls::Visibility::VISIBLE);
    m_scrollView->setVisibility(brls::Visibility::GONE);
    m_emptyStateBox->setVisibility(brls::Visibility::GONE);
    m_titleLabel->setText("Reading History (Loading...)");

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<ReadingHistoryItem> history;

        // Fetch initial batch
        bool success = client.fetchReadingHistory(0, ITEMS_PER_PAGE, history);

        brls::sync([this, history, success, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            m_loaded = true;
            m_loadingLabel->setVisibility(brls::Visibility::GONE);

            // Handle load failure (offline or server error)
            if (!success) {
                m_scrollView->setVisibility(brls::Visibility::GONE);
                m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
                m_titleLabel->setText("Reading History");
                brls::Application::notify("Failed to load history - check connection");
                return;
            }

            m_historyItems = history;
            m_currentOffset = static_cast<int>(history.size());
            m_hasMoreItems = (history.size() >= ITEMS_PER_PAGE);

            rebuildHistoryList();
        });
    });
}

void HistoryTab::loadMoreHistory() {
    if (!m_hasMoreItems) return;

    brls::Logger::debug("HistoryTab: Loading more history from offset {}...", m_currentOffset);

    // Remember the index of first new item (current list size)
    size_t firstNewItemIndex = m_historyItems.size();

    // Update load more button
    if (m_loadMoreBtn) {
        m_loadMoreBtn->setText("Loading...");
    }

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak, firstNewItemIndex]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<ReadingHistoryItem> moreHistory;

        bool success = client.fetchReadingHistory(m_currentOffset, ITEMS_PER_PAGE, moreHistory);

        brls::sync([this, moreHistory, success, aliveWeak, firstNewItemIndex]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (success && !moreHistory.empty()) {
                // Append to existing items in data model
                for (const auto& item : moreHistory) {
                    m_historyItems.push_back(item);
                }
                m_currentOffset += static_cast<int>(moreHistory.size());
                m_hasMoreItems = (moreHistory.size() >= ITEMS_PER_PAGE);

                // Incrementally append items to UI (no rebuild needed)
                appendHistoryItems(moreHistory, firstNewItemIndex);
            } else {
                m_hasMoreItems = false;
                if (m_loadMoreBtn) {
                    m_loadMoreBtn->setVisibility(brls::Visibility::GONE);
                }
            }

            brls::Logger::info("HistoryTab: Now have {} history items", m_historyItems.size());
        });
    });
}

void HistoryTab::rebuildHistoryList() {
    m_contentBox->clearViews();
    m_itemRows.clear();
    m_loadMoreBtn = nullptr;

    if (m_historyItems.empty()) {
        m_scrollView->setVisibility(brls::Visibility::GONE);
        m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
        m_titleLabel->setText("Reading History");
        m_focusIndexAfterRebuild = -1;
        return;
    }

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

        // Create history item row using helper
        auto* itemRow = createHistoryItemRow(item, static_cast<int>(i));
        m_contentBox->addView(itemRow);
        m_itemRows.push_back(itemRow);
    }

    // Add Load More button if there are more items
    if (m_hasMoreItems) {
        m_loadMoreBtn = new brls::Button();
        m_loadMoreBtn->setText("Load More");
        m_loadMoreBtn->setMarginTop(15);
        m_loadMoreBtn->setMarginBottom(15);
        m_loadMoreBtn->registerClickAction([this](brls::View*) {
            loadMoreHistory();
            return true;
        });
        m_loadMoreBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_loadMoreBtn));
        m_contentBox->addView(m_loadMoreBtn);
    }

    // Focus on first new item after load more, or first item after initial load
    if (m_focusIndexAfterRebuild >= 0 && m_focusIndexAfterRebuild < static_cast<int>(m_itemRows.size())) {
        brls::Application::giveFocus(m_itemRows[m_focusIndexAfterRebuild]);
        m_focusIndexAfterRebuild = -1;
    } else if (!m_itemRows.empty() && m_focusIndexAfterRebuild == -1) {
        // Initial load - focus on first item
        brls::Application::giveFocus(m_itemRows[0]);
    }

    brls::Logger::info("HistoryTab: Displayed {} history items", m_historyItems.size());
}

void HistoryTab::onHistoryItemSelected(const ReadingHistoryItem& item) {
    brls::Logger::info("HistoryTab: Resume reading '{}' chapter {} (id={}) at page {}",
                       item.mangaTitle, item.chapterNumber, item.chapterId, item.lastPageRead);

    // Resume reading from last page using chapter ID (not chapter number)
    Application::getInstance().pushReaderActivityAtPage(
        item.mangaId,
        item.chapterId,  // Use chapter ID for proper chapter identification
        item.lastPageRead,
        item.mangaTitle
    );
}

void HistoryTab::showHistoryItemMenu(const ReadingHistoryItem& item, int index) {
    std::vector<std::string> options = {
        "Continue Reading",
        "View Manga Details",
        "Mark as Unread"
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
                    case 2:  // Mark as Unread
                        markChapterUnread(item);
                        break;
                }
            });
        }
    );
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void HistoryTab::markChapterUnread(const ReadingHistoryItem& item) {
    brls::Logger::info("HistoryTab: Marking chapter {} as unread", item.chapterId);

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, item, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        // Mark chapter as unread - this effectively removes it from history
        bool success = client.markChapterUnread(item.mangaId, item.chapterId);

        brls::sync([this, item, success, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (success) {
                // Remove from local list
                for (auto it = m_historyItems.begin(); it != m_historyItems.end(); ++it) {
                    if (it->chapterId == item.chapterId) {
                        m_historyItems.erase(it);
                        break;
                    }
                }
                rebuildHistoryList();
                brls::Application::notify("Marked as unread");
            } else {
                brls::Application::notify("Failed to mark as unread");
            }
        });
    });
}

std::string HistoryTab::formatTimestamp(int64_t timestamp) {
    if (timestamp <= 0) return "Unknown date";

    time_t itemTime = static_cast<time_t>(timestamp / 1000);  // Convert from milliseconds
    time_t now = std::time(nullptr);

    // Calculate days difference using actual time difference (handles year boundaries)
    // 86400 = seconds in a day
    int64_t timeDiff = now - itemTime;
    int daysDiff = static_cast<int>(timeDiff / 86400);

    // Get the tm struct for the item time (for day name and date formatting)
    struct tm* tm_info = localtime(&itemTime);
    if (!tm_info) return "Unknown date";

    // Make a copy since localtime uses static buffer
    struct tm itemTm = *tm_info;

    // Check same calendar day (not just within 24 hours)
    struct tm* now_tm = localtime(&now);
    if (!now_tm) return "Unknown date";

    // Check if it's today (same year, month, day)
    if (itemTm.tm_year == now_tm->tm_year &&
        itemTm.tm_mon == now_tm->tm_mon &&
        itemTm.tm_mday == now_tm->tm_mday) {
        return "Today";
    }

    // Check if it's yesterday (within 24-48 hours and different calendar day)
    // Or check by decrementing today's date
    time_t yesterdayTime = now - 86400;
    struct tm* yesterday_tm = localtime(&yesterdayTime);
    if (yesterday_tm &&
        itemTm.tm_year == yesterday_tm->tm_year &&
        itemTm.tm_mon == yesterday_tm->tm_mon &&
        itemTm.tm_mday == yesterday_tm->tm_mday) {
        return "Yesterday";
    }

    // Check if it's within this week (less than 7 days ago)
    if (daysDiff >= 0 && daysDiff < 7) {
        const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        return days[itemTm.tm_wday];
    }

    // Format as date
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%b %d, %Y", &itemTm);
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

brls::Box* HistoryTab::createHistoryItemRow(const ReadingHistoryItem& item, int index) {
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

    return itemRow;
}

void HistoryTab::appendHistoryItems(const std::vector<ReadingHistoryItem>& items, size_t startIndex) {
    if (items.empty()) return;

    // Get the last date from existing items to check if we need date headers
    std::string lastDate;
    if (!m_historyItems.empty() && startIndex > 0) {
        lastDate = formatTimestamp(m_historyItems[startIndex - 1].lastReadAt);
    }

    // Remove Load More button temporarily (we'll add it back at the end)
    if (m_loadMoreBtn && m_loadMoreBtn->getParent() == m_contentBox) {
        m_contentBox->removeView(m_loadMoreBtn);
        m_loadMoreBtn = nullptr;
    }

    // Add the new items
    for (size_t i = 0; i < items.size(); i++) {
        const auto& item = items[i];
        size_t globalIndex = startIndex + i;

        // Date header if date changed
        std::string dateStr = formatTimestamp(item.lastReadAt);
        if (dateStr != lastDate) {
            lastDate = dateStr;

            auto* dateHeader = new brls::Label();
            dateHeader->setText(dateStr);
            dateHeader->setFontSize(16);
            dateHeader->setTextColor(nvgRGB(0, 150, 136));  // Teal
            dateHeader->setMarginTop(globalIndex > 0 ? 20 : 5);
            dateHeader->setMarginBottom(8);
            m_contentBox->addView(dateHeader);
        }

        // Create the item row
        auto* itemRow = createHistoryItemRow(item, static_cast<int>(globalIndex));
        m_contentBox->addView(itemRow);
        m_itemRows.push_back(itemRow);
    }

    // Add Load More button if there are more items
    if (m_hasMoreItems) {
        m_loadMoreBtn = new brls::Button();
        m_loadMoreBtn->setText("Load More");
        m_loadMoreBtn->setMarginTop(15);
        m_loadMoreBtn->setMarginBottom(15);
        m_loadMoreBtn->registerClickAction([this](brls::View*) {
            loadMoreHistory();
            return true;
        });
        m_loadMoreBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_loadMoreBtn));
        m_contentBox->addView(m_loadMoreBtn);
    }

    // Update title with new count
    m_titleLabel->setText("Reading History (" + std::to_string(m_historyItems.size()) + ")");

    // Focus on first newly added item
    if (!items.empty() && startIndex < m_itemRows.size()) {
        brls::Application::giveFocus(m_itemRows[startIndex]);
    }

    brls::Logger::info("HistoryTab: Appended {} items, now have {} total", items.size(), m_historyItems.size());
}

} // namespace vitasuwayomi
