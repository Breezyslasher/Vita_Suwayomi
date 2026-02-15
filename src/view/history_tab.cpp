/**
 * VitaSuwayomi - History Tab implementation
 * Shows chronological reading history with date/time and quick resume.
 *
 * Loading strategy:
 *   1. Show cached data instantly from disk (no network wait)
 *   2. Fetch ALL items from server in background (single request, up to 500)
 *   3. Update cache and rebuild list only if data changed
 *
 * No pagination / loadMore needed — everything fetched at once.
 */

#include "view/history_tab.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/async.hpp"
#include "utils/image_loader.hpp"
#include "utils/library_cache.hpp"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace vitasuwayomi {

// Fetch up to 500 items in one shot (same limit library tab uses)
static constexpr int MAX_HISTORY_ITEMS = 500;

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

    // Step 1: Try to show cached data instantly
    loadFromCache();

    // Step 2: Fetch fresh data from server in background
    fetchFromServer();
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
        loadFromCache();
        fetchFromServer();
    }
}

void HistoryTab::refresh() {
    // Move focus to refresh button before clearing items
    if (m_refreshBtn) {
        brls::Application::giveFocus(m_refreshBtn);
    }

    m_loaded = false;
    m_historyItems.clear();

    // Show loading state
    m_loadingLabel->setVisibility(brls::Visibility::VISIBLE);
    m_scrollView->setVisibility(brls::Visibility::GONE);
    m_emptyStateBox->setVisibility(brls::Visibility::GONE);
    m_titleLabel->setText("Reading History (Refreshing...)");

    // Fetch fresh from server
    fetchFromServer();
}

void HistoryTab::loadFromCache() {
    LibraryCache& cache = LibraryCache::getInstance();
    std::vector<ReadingHistoryItem> cached;

    if (cache.loadHistory(cached) && !cached.empty()) {
        brls::Logger::info("HistoryTab: Loaded {} items from cache", cached.size());
        m_historyItems = cached;
        m_loaded = true;
        buildList();
    } else {
        // No cache — show loading spinner until server responds
        m_loadingLabel->setVisibility(brls::Visibility::VISIBLE);
        m_scrollView->setVisibility(brls::Visibility::GONE);
        m_titleLabel->setText("Reading History (Loading...)");
    }
}

void HistoryTab::fetchFromServer() {
    if (m_isFetching) return;
    m_isFetching = true;

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<ReadingHistoryItem> history;

        // Fetch everything in one request
        bool success = client.fetchReadingHistory(0, MAX_HISTORY_ITEMS, history);

        // Sort newest first (server should already do this, but ensure it)
        if (success) {
            std::sort(history.begin(), history.end(),
                [](const ReadingHistoryItem& a, const ReadingHistoryItem& b) {
                    return a.lastReadAt > b.lastReadAt;
                });

            // Save to disk cache (on background thread — no UI impact)
            LibraryCache::getInstance().saveHistory(history);
        }

        brls::sync([this, history, success, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            m_isFetching = false;

            if (!success) {
                // Server failed — keep showing cached data if we have it
                if (m_historyItems.empty()) {
                    m_loadingLabel->setVisibility(brls::Visibility::GONE);
                    m_scrollView->setVisibility(brls::Visibility::GONE);
                    m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
                    m_titleLabel->setText("Reading History");
                    brls::Application::notify("Failed to load history - check connection");
                } else {
                    // Already showing cached data, just notify
                    m_titleLabel->setText("Reading History (" + std::to_string(m_historyItems.size()) + ")");
                    brls::Application::notify("Using cached data - server unavailable");
                }
                return;
            }

            // Check if data actually changed (avoid unnecessary rebuild)
            bool changed = (history.size() != m_historyItems.size());
            if (!changed && !history.empty()) {
                // Quick check: compare first and last item timestamps
                changed = (history.front().lastReadAt != m_historyItems.front().lastReadAt) ||
                          (history.back().lastReadAt != m_historyItems.back().lastReadAt);
            }

            m_historyItems = history;
            m_loaded = true;

            if (changed || m_contentBox->getChildren().empty()) {
                buildList();
            } else {
                // Data unchanged — just update title
                m_titleLabel->setText("Reading History (" + std::to_string(m_historyItems.size()) + ")");
            }

            brls::Logger::info("HistoryTab: Server returned {} items (changed={})", history.size(), changed);
        });
    });
}

void HistoryTab::buildList() {
    // Move focus to refresh button before clearing to prevent crash
    if (m_refreshBtn && !m_itemRows.empty()) {
        brls::Application::giveFocus(m_refreshBtn);
    }

    m_contentBox->clearViews();
    m_itemRows.clear();

    m_loadingLabel->setVisibility(brls::Visibility::GONE);

    if (m_historyItems.empty()) {
        m_scrollView->setVisibility(brls::Visibility::GONE);
        m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
        m_titleLabel->setText("Reading History");
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

        // Create history item row
        auto* itemRow = createHistoryItemRow(item, static_cast<int>(i));
        m_contentBox->addView(itemRow);
        m_itemRows.push_back(itemRow);
    }

    // Set up navigation between refresh button and first history item
    if (!m_itemRows.empty() && m_refreshBtn) {
        m_itemRows[0]->setCustomNavigationRoute(brls::FocusDirection::UP, m_refreshBtn);
        m_refreshBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, m_itemRows[0]);
    }

    // Focus on first item
    if (!m_itemRows.empty()) {
        brls::Application::giveFocus(m_itemRows[0]);
    }

    brls::Logger::info("HistoryTab: Displayed {} history items", m_historyItems.size());
}

void HistoryTab::onHistoryItemSelected(const ReadingHistoryItem& item) {
    brls::Logger::info("HistoryTab: Resume reading '{}' chapter {} (id={}) at page {}",
                       item.mangaTitle, item.chapterNumber, item.chapterId, item.lastPageRead);

    Application::getInstance().pushReaderActivityAtPage(
        item.mangaId,
        item.chapterId,
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
                // Update cache
                LibraryCache::getInstance().saveHistory(m_historyItems);
                buildList();
                brls::Application::notify("Marked as unread");
            } else {
                brls::Application::notify("Failed to mark as unread");
            }
        });
    });
}

std::string HistoryTab::formatTimestamp(int64_t timestamp) {
    if (timestamp <= 0) return "Unknown date";

    time_t itemTime;
    if (timestamp > 100000000000LL) {
        itemTime = static_cast<time_t>(timestamp / 1000);
    } else {
        itemTime = static_cast<time_t>(timestamp);
    }
    time_t now = std::time(nullptr);

    int64_t timeDiff = now - itemTime;
    int daysDiff = static_cast<int>(timeDiff / 86400);

    struct tm* tm_info = localtime(&itemTime);
    if (!tm_info) return "Unknown date";
    struct tm itemTm = *tm_info;

    struct tm* now_tm = localtime(&now);
    if (!now_tm) return "Unknown date";

    if (itemTm.tm_year == now_tm->tm_year &&
        itemTm.tm_mon == now_tm->tm_mon &&
        itemTm.tm_mday == now_tm->tm_mday) {
        return "Today";
    }

    time_t yesterdayTime = now - 86400;
    struct tm* yesterday_tm = localtime(&yesterdayTime);
    if (yesterday_tm &&
        itemTm.tm_year == yesterday_tm->tm_year &&
        itemTm.tm_mon == yesterday_tm->tm_mon &&
        itemTm.tm_mday == yesterday_tm->tm_mday) {
        return "Yesterday";
    }

    if (daysDiff >= 0 && daysDiff < 7) {
        const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        return days[itemTm.tm_wday];
    }

    char buffer[64];
    strftime(buffer, sizeof(buffer), "%b %d, %Y", &itemTm);
    return std::string(buffer);
}

std::string HistoryTab::formatRelativeTime(int64_t timestamp) {
    if (timestamp <= 0) return "";

    time_t time;
    if (timestamp > 100000000000LL) {
        time = static_cast<time_t>(timestamp / 1000);
    } else {
        time = static_cast<time_t>(timestamp);
    }
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

} // namespace vitasuwayomi
