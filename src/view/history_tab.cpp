/**
 * VitaSuwayomi - History Tab implementation
 *
 * Direction B: a dense, day-grouped reading-history list with client-side
 * quick filters (All / This week / Unfinished) and done/unfinished markers.
 * The per-item menu is homed on the shared OptionsPopover for visual
 * consistency with the rest of the app.
 */

#include "view/history_tab.hpp"
#include "view/options_popover.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/async.hpp"
#include "utils/image_loader.hpp"
#include "utils/button_icons.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace vitasuwayomi {

namespace {

// Direction B palette (see the handoff). Kept local so the tab owns its look.
namespace hc {
    inline NVGcolor accent()   { return nvgRGB(0x64, 0xB4, 0xFF); }  // #64B4FF
    inline NVGcolor accentInk(){ return nvgRGB(0x0d, 0x22, 0x36); }  // #0d2236
    inline NVGcolor success()  { return nvgRGB(0x4E, 0xCC, 0xA3); }  // #4ECCA3
    inline NVGcolor muted()    { return nvgRGB(0x8b, 0x8b, 0x93); }  // #8b8b93
    inline NVGcolor body()     { return nvgRGB(0xC5, 0xC6, 0xD0); }  // #C5C6D0
    inline NVGcolor heading()  { return nvgRGB(0xE7, 0xE7, 0xEA); }  // #E7E7EA
    inline NVGcolor chipIdle()  { return nvgRGB(0x26, 0x26, 0x2a); }
}

// Small nanovg-drawn trailing marker: a check (done) or a right-pointing play
// triangle (unfinished). Avoids needing tinted PNGs.
class HistMarker : public brls::Box {
public:
    HistMarker(bool done, NVGcolor color) : m_done(done), m_color(color) {}
    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
        brls::Box::draw(vg, x, y, w, h, style, ctx);
        const float s  = (w < h) ? w : h;
        const float cx = x + w * 0.5f, cy = y + h * 0.5f;
        if (m_done) {
            nvgBeginPath(vg);
            nvgStrokeColor(vg, m_color);
            nvgStrokeWidth(vg, s * 0.12f);
            nvgLineCap(vg, NVG_ROUND);
            nvgLineJoin(vg, NVG_ROUND);
            nvgMoveTo(vg, cx - s * 0.26f, cy + s * 0.02f);
            nvgLineTo(vg, cx - s * 0.06f, cy + s * 0.22f);
            nvgLineTo(vg, cx + s * 0.28f, cy - s * 0.20f);
            nvgStroke(vg);
        } else {
            // Filled play triangle pointing right.
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx - s * 0.20f, cy - s * 0.26f);
            nvgLineTo(vg, cx + s * 0.28f, cy);
            nvgLineTo(vg, cx - s * 0.20f, cy + s * 0.26f);
            nvgClosePath(vg);
            nvgFillColor(vg, m_color);
            nvgFill(vg);
        }
    }
private:
    bool     m_done;
    NVGcolor m_color;
};

} // namespace

HistoryTab::HistoryTab() {
    m_alive = std::make_shared<bool>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Header row with title, count pill and refresh button
    auto* headerRow = new brls::Box();
    headerRow->setAxis(brls::Axis::ROW);
    headerRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    headerRow->setAlignItems(brls::AlignItems::CENTER);
    headerRow->setMarginBottom(12);

    // Title + count pill (left)
    auto* titleGroup = new brls::Box();
    titleGroup->setAxis(brls::Axis::ROW);
    titleGroup->setAlignItems(brls::AlignItems::CENTER);
    titleGroup->setGrow(1.0f);

    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Reading History");
    m_titleLabel->setFontSize(28);
    titleGroup->addView(m_titleLabel);

    m_countPill = new brls::Box();
    m_countPill->setAxis(brls::Axis::ROW);
    m_countPill->setJustifyContent(brls::JustifyContent::CENTER);
    m_countPill->setAlignItems(brls::AlignItems::CENTER);
    m_countPill->setHeight(24);
    m_countPill->setCornerRadius(12);
    m_countPill->setPaddingLeft(10);
    m_countPill->setPaddingRight(10);
    m_countPill->setMarginLeft(10);
    m_countPill->setBackgroundColor(hc::chipIdle());
    m_countPillLabel = new brls::Label();
    m_countPillLabel->setText("0");
    m_countPillLabel->setFontSize(13);
    m_countPillLabel->setTextColor(hc::body());
    m_countPill->addView(m_countPillLabel);
    titleGroup->addView(m_countPill);

    headerRow->addView(titleGroup);

    // Refresh button with triangle hint
    auto* refreshContainer = new brls::Box();
    refreshContainer->setAxis(brls::Axis::COLUMN);
    refreshContainer->setAlignItems(brls::AlignItems::CENTER);

    auto* triangleIcon = new brls::Image();
    triangleIcon->setWidth(16);
    triangleIcon->setHeight(16);
    triangleIcon->setScalingType(brls::ImageScalingType::FIT);
    setButtonIcon(triangleIcon, BUTTON_IMG(BUTTON_Y_ICON));
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
    refreshIcon->setImageFromFile(RESOURCE_PREFIX "icons/refresh.png");
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
    emptyIcon->setTextColor(Application::getInstance().getDimTextColor());
    emptyIcon->setMarginBottom(10);
    m_emptyStateBox->addView(emptyIcon);

    auto* emptyHint = new brls::Label();
    emptyHint->setText("Start reading manga to see your history here");
    emptyHint->setFontSize(16);
    emptyHint->setTextColor(Application::getInstance().getDimTextColor());
    m_emptyStateBox->addView(emptyHint);

    this->addView(m_emptyStateBox);

    // Loading indicator (centered, hidden by default)
    m_loadingLabel = new brls::Label();
    m_loadingLabel->setText("Loading history...");
    m_loadingLabel->setFontSize(18);
    m_loadingLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_loadingLabel->setTextColor(Application::getInstance().getSubtitleColor());
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
    if (m_uploadsDeferred) {
        ImageLoader::setDeferTextureUploads(false);
    }
    brls::Logger::debug("HistoryTab: Destroyed");
}

void HistoryTab::draw(NVGcontext* vg, float x, float y, float width, float height,
                      brls::Style style, brls::FrameContext* ctx) {
    // Pause ImageLoader GPU texture uploads while actively scrolling fast.
    // Each upload costs ~15-20ms on Vita and stalls the frame.
    if (m_scrollView) {
        float curY = m_scrollView->getContentOffsetY();
        float frameDelta = std::fabs(curY - m_prevScrollY);
        m_prevScrollY = curY;
        bool movingFast = frameDelta > 28.0f;
        if (movingFast) {
            m_scrollSettledFrames = 0;
        } else {
            m_scrollSettledFrames++;
        }
        bool wantDefer = movingFast || m_scrollSettledFrames < 3;
        if (wantDefer != m_uploadsDeferred) {
            m_uploadsDeferred = wantDefer;
            ImageLoader::setDeferTextureUploads(wantDefer);
        }

        // Visibility culling: hide off-screen rows so borealis skips their
        // entire View::frame() call tree.  INVISIBLE keeps layout space but
        // skips all NVG work (no yoga invalidation either).
        float viewportH = m_scrollView->getHeight();
        float svTop     = m_scrollView->getY();
        float svBot     = svTop + viewportH;
        float pad       = viewportH;

        for (brls::View* child : m_contentBox->getChildren()) {
            float cy = child->getY();
            float ch = child->getHeight();
            bool onScreen = (cy + ch >= svTop - pad && cy <= svBot + pad);
            auto cur = child->getVisibility();
            if (onScreen && cur == brls::Visibility::INVISIBLE)
                child->setVisibility(brls::Visibility::VISIBLE);
            else if (!onScreen && cur == brls::Visibility::VISIBLE)
                child->setVisibility(brls::Visibility::INVISIBLE);
        }
    }

    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

void HistoryTab::willDisappear(bool resetState) {
    brls::Box::willDisappear(resetState);

    // Invalidate alive flag BEFORE destruction so pending async callbacks bail out
    if (m_alive) *m_alive = false;

    // Cancel pending image loads to free up worker threads and network bandwidth
    ImageLoader::cancelAll();
    if (m_uploadsDeferred) {
        m_uploadsDeferred = false;
        ImageLoader::setDeferTextureUploads(false);
    }
    m_isLoadingMore = false;
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

bool HistoryTab::itemIsDone(const ReadingHistoryItem& item) {
    return item.pageCount > 0 && (item.lastPageRead + 1) >= item.pageCount;
}

void HistoryTab::loadHistory() {
    brls::Logger::debug("HistoryTab: Loading history...");

    // Show loading indicator
    m_loadingLabel->setVisibility(brls::Visibility::VISIBLE);
    m_scrollView->setVisibility(brls::Visibility::GONE);
    m_emptyStateBox->setVisibility(brls::Visibility::GONE);
    m_titleLabel->setText("Reading History");
    if (m_countPillLabel) m_countPillLabel->setText("...");

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
                if (m_countPillLabel) m_countPillLabel->setText("0");
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

    // Remember the number of rows currently displayed so the incremental
    // append (All filter) knows where the new rows begin.
    size_t firstNewDisplayedIndex = m_itemRows.size();

    std::weak_ptr<bool> aliveWeak = m_alive;
    int offset = m_currentOffset;  // Capture by value for safe background access

    asyncRun([this, aliveWeak, firstNewDisplayedIndex, offset]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<ReadingHistoryItem> moreHistory;

        bool success = client.fetchReadingHistory(offset, ITEMS_PER_PAGE, moreHistory);

        brls::sync([this, moreHistory, success, aliveWeak, firstNewDisplayedIndex]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (success && !moreHistory.empty()) {
                // Append to existing items in the data model
                for (const auto& item : moreHistory) {
                    m_historyItems.push_back(item);
                }
                m_currentOffset += static_cast<int>(moreHistory.size());
                m_hasMoreItems = (moreHistory.size() >= ITEMS_PER_PAGE);

                // Incrementally append the new rows.
                appendHistoryItems(moreHistory, firstNewDisplayedIndex);
            } else {
                m_hasMoreItems = false;
            }

            m_isLoadingMore = false;
            brls::Logger::info("HistoryTab: Now have {} history items", m_historyItems.size());
        });
    });
}

void HistoryTab::rebuildHistoryList() {
    m_contentBox->clearViews();
    m_itemRows.clear();

    const std::vector<ReadingHistoryItem>& items = m_historyItems;

    if (m_countPillLabel)
        m_countPillLabel->setText(std::to_string(items.size()));

    if (items.empty()) {
        m_scrollView->setVisibility(brls::Visibility::GONE);
        m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
        m_titleLabel->setText("Reading History");
        m_focusIndexAfterRebuild = -1;
        return;
    }

    m_scrollView->setVisibility(brls::Visibility::VISIBLE);
    m_emptyStateBox->setVisibility(brls::Visibility::GONE);
    m_titleLabel->setText("Reading History");

    // Group by date
    std::string lastDate;
    for (size_t i = 0; i < items.size(); i++) {
        const auto& item = items[i];

        // Date header with a per-group count
        std::string dateStr = formatTimestamp(item.lastReadAt);
        if (dateStr != lastDate) {
            lastDate = dateStr;

            // Count how many consecutive items share this date.
            int groupCount = 0;
            for (size_t j = i; j < items.size() &&
                 formatTimestamp(items[j].lastReadAt) == dateStr; j++) {
                groupCount++;
            }

            auto* header = new brls::Box();
            header->setAxis(brls::Axis::ROW);
            header->setAlignItems(brls::AlignItems::CENTER);
            header->setMarginTop(i > 0 ? 18 : 4);
            header->setMarginBottom(8);

            auto* dateHeader = new brls::Label();
            dateHeader->setText(dateStr);
            dateHeader->setFontSize(15);
            dateHeader->setTextColor(Application::getInstance().getTealColor());
            header->addView(dateHeader);

            auto* groupCountLabel = new brls::Label();
            groupCountLabel->setText(std::to_string(groupCount));
            groupCountLabel->setFontSize(12);
            groupCountLabel->setTextColor(hc::muted());
            groupCountLabel->setMarginLeft(8);
            header->addView(groupCountLabel);

            m_contentBox->addView(header);
        }

        auto* itemRow = createHistoryItemRow(item, static_cast<int>(i));
        m_contentBox->addView(itemRow);
        m_itemRows.push_back(itemRow);
    }

    // Navigation between the refresh button and the first history item
    if (!m_itemRows.empty() && m_refreshBtn) {
        m_itemRows[0]->setCustomNavigationRoute(brls::FocusDirection::UP, m_refreshBtn);
        m_refreshBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, m_itemRows[0]);
    }

    // Focus on first new item after load-more, or first item after initial load
    if (m_focusIndexAfterRebuild >= 0 && m_focusIndexAfterRebuild < static_cast<int>(m_itemRows.size())) {
        brls::Application::giveFocus(m_itemRows[m_focusIndexAfterRebuild]);
        m_focusIndexAfterRebuild = -1;
    } else if (!m_itemRows.empty() && m_focusIndexAfterRebuild == -1) {
        brls::Application::giveFocus(m_itemRows[0]);
    }

    brls::Logger::info("HistoryTab: Displayed {} history items", m_itemRows.size());
}

void HistoryTab::onHistoryItemSelected(const ReadingHistoryItem& item) {
    brls::Logger::info("HistoryTab: Resume reading '{}' chapter {} (id={}) at page {}",
                       item.mangaTitle, item.chapterNumber, item.chapterId, item.lastPageRead);

    // Resume reading from last page using chapter ID (not chapter number)
    Application::getInstance().pushReaderActivityAtPage(
        item.mangaId,
        item.chapterId,
        item.lastPageRead,
        item.mangaTitle
    );
}

void HistoryTab::showHistoryItemMenu(const ReadingHistoryItem& item, int /*index*/) {
    std::string sub = "Ch. " + std::to_string(static_cast<int>(item.chapterNumber)) +
                      " · page " + std::to_string(item.lastPageRead + 1);
    if (item.pageCount > 0) sub += " / " + std::to_string(item.pageCount);

    std::vector<OptionRow> rows;

    OptionRow cont;
    cont.icon    = "book-open-page-variant.png";
    cont.label   = "Continue reading";
    cont.sub     = "p. " + std::to_string(item.lastPageRead + 1);
    cont.primary = true;
    cont.action  = [this, item]() { onHistoryItemSelected(item); };
    rows.push_back(cont);

    OptionRow details;
    details.icon   = "book-multiple.png";
    details.label  = "View manga details";
    details.action = [item]() {
        Application::getInstance().pushMangaDetailView(item.mangaId);
    };
    rows.push_back(details);

    OptionRow unread;
    unread.icon   = "cross.png";  // drawn as a crisp MDI vector glyph
    unread.label  = "Mark as unread";
    unread.danger = true;
    unread.action = [this, item]() { markChapterUnread(item); };
    rows.push_back(unread);

    OptionsPopover::show("HISTORY", item.mangaTitle, std::move(rows));
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
                // Move focus off any row before the rebuild destroys it.
                if (m_refreshBtn) brls::Application::giveFocus(m_refreshBtn);

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

    // Detect if timestamp is in milliseconds or seconds
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

brls::Box* HistoryTab::createHistoryItemRow(const ReadingHistoryItem& item, int displayedIndex) {
    const bool done = itemIsDone(item);

    auto* itemRow = new brls::Box();
    itemRow->setAxis(brls::Axis::ROW);
    itemRow->setAlignItems(brls::AlignItems::CENTER);
    itemRow->setPadding(9);
    itemRow->setMarginBottom(6);
    itemRow->setCornerRadius(10);
    itemRow->setBackgroundColor(Application::getInstance().getRowBackground());
    itemRow->setFocusable(true);
    itemRow->setHeight(72);

    // Cover image (38x54)
    auto* coverImage = new brls::Image();
    coverImage->setWidth(38);
    coverImage->setHeight(54);
    coverImage->setCornerRadius(4);
    coverImage->setScalingType(brls::ImageScalingType::FILL);
    coverImage->setMarginRight(12);

    if (!item.mangaThumbnail.empty()) {
        std::string url = item.mangaThumbnail;
        if (url[0] == '/') {
            url = SuwayomiClient::getInstance().getServerUrl() + url;
        }
        ImageLoader::loadAsync(url, nullptr, coverImage, m_alive);
    }
    itemRow->addView(coverImage);

    // Info column (title + chapter)
    auto* infoBox = new brls::Box();
    infoBox->setAxis(brls::Axis::COLUMN);
    infoBox->setGrow(1.0f);
    infoBox->setJustifyContent(brls::JustifyContent::CENTER);

    auto* titleLabel = new brls::Label();
    std::string title = item.mangaTitle;
    if (title.length() > 38) {
        title = title.substr(0, 36) + "..";
    }
    titleLabel->setText(title);
    titleLabel->setFontSize(14.5f);
    titleLabel->setTextColor(hc::heading());
    titleLabel->setSingleLine(true);
    infoBox->addView(titleLabel);

    auto* chapterLabel = new brls::Label();
    std::string chapterText = item.chapterName;
    if (chapterText.empty()) {
        chapterText = "Chapter " + std::to_string(static_cast<int>(item.chapterNumber));
    }
    chapterLabel->setText(chapterText);
    chapterLabel->setFontSize(12.5f);
    chapterLabel->setTextColor(hc::muted());
    chapterLabel->setSingleLine(true);
    chapterLabel->setMarginTop(2);
    infoBox->addView(chapterLabel);

    itemRow->addView(infoBox);

    // Right column: page (done->success else body) over relative time (dim)
    auto* rightBox = new brls::Box();
    rightBox->setAxis(brls::Axis::COLUMN);
    rightBox->setAlignItems(brls::AlignItems::FLEX_END);
    rightBox->setJustifyContent(brls::JustifyContent::CENTER);
    rightBox->setMarginLeft(8);

    auto* pageLabel = new brls::Label();
    std::string pageText = "p. " + std::to_string(item.lastPageRead + 1);
    if (item.pageCount > 0) pageText += " / " + std::to_string(item.pageCount);
    pageLabel->setText(pageText);
    pageLabel->setFontSize(13);
    pageLabel->setTextColor(done ? hc::success() : hc::body());
    pageLabel->setSingleLine(true);
    rightBox->addView(pageLabel);

    auto* timeLabel = new brls::Label();
    timeLabel->setText(formatRelativeTime(item.lastReadAt));
    timeLabel->setFontSize(12);
    timeLabel->setTextColor(Application::getInstance().getDimTextColor());
    timeLabel->setSingleLine(true);
    timeLabel->setMarginTop(2);
    rightBox->addView(timeLabel);

    itemRow->addView(rightBox);

    // Trailing marker: check (done) or play (unfinished)
    auto* marker = new HistMarker(done, done ? hc::success() : hc::accent());
    marker->setWidth(22);
    marker->setHeight(22);
    marker->setMarginLeft(12);
    itemRow->addView(marker);

    // Click to resume reading
    ReadingHistoryItem capturedItem = item;
    itemRow->registerClickAction([this, capturedItem](brls::View* view) {
        onHistoryItemSelected(capturedItem);
        return true;
    });
    itemRow->addGestureRecognizer(new brls::TapGestureRecognizer(itemRow));

    // Start button opens the OptionsPopover menu
    itemRow->registerAction("Menu", brls::ControllerButton::BUTTON_START,
                            [this, capturedItem, displayedIndex](brls::View*) {
        showHistoryItemMenu(capturedItem, displayedIndex);
        return true;
    });

    // Infinite scroll: auto-load more when focus nears the end of the list.
    itemRow->getFocusEvent()->subscribe([this, displayedIndex](brls::View*) {
        if (m_hasMoreItems && !m_isLoadingMore) {
            const int threshold = 5;
            if (displayedIndex >= static_cast<int>(m_itemRows.size()) - threshold) {
                m_isLoadingMore = true;
                loadMoreHistory();
            }
        }
    });

    return itemRow;
}

void HistoryTab::appendHistoryItems(const std::vector<ReadingHistoryItem>& items, size_t startIndex) {
    if (items.empty()) return;

    // Derive the last shown date from the previous last displayed item so we
    // only add a new date header when the date actually changes.
    std::string lastDate;
    if (startIndex > 0 && startIndex <= m_historyItems.size()) {
        lastDate = formatTimestamp(m_historyItems[startIndex - 1].lastReadAt);
    }

    for (size_t i = 0; i < items.size(); i++) {
        const auto& item = items[i];
        size_t displayedIndex = startIndex + i;

        std::string dateStr = formatTimestamp(item.lastReadAt);
        if (dateStr != lastDate) {
            lastDate = dateStr;

            auto* header = new brls::Box();
            header->setAxis(brls::Axis::ROW);
            header->setAlignItems(brls::AlignItems::CENTER);
            header->setMarginTop(displayedIndex > 0 ? 18 : 4);
            header->setMarginBottom(8);

            auto* dateHeader = new brls::Label();
            dateHeader->setText(dateStr);
            dateHeader->setFontSize(15);
            dateHeader->setTextColor(Application::getInstance().getTealColor());
            header->addView(dateHeader);

            // Count remaining new items that share this date.
            int groupCount = 0;
            for (size_t j = i; j < items.size() &&
                 formatTimestamp(items[j].lastReadAt) == dateStr; j++) {
                groupCount++;
            }
            auto* groupCountLabel = new brls::Label();
            groupCountLabel->setText(std::to_string(groupCount));
            groupCountLabel->setFontSize(12);
            groupCountLabel->setTextColor(hc::muted());
            groupCountLabel->setMarginLeft(8);
            header->addView(groupCountLabel);

            m_contentBox->addView(header);
        }

        auto* itemRow = createHistoryItemRow(item, static_cast<int>(displayedIndex));
        m_contentBox->addView(itemRow);
        m_itemRows.push_back(itemRow);
    }

    // Update the count pill with the new displayed total
    if (m_countPillLabel)
        m_countPillLabel->setText(std::to_string(m_itemRows.size()));

    // Focus on first newly added item
    if (startIndex < m_itemRows.size()) {
        brls::Application::giveFocus(m_itemRows[startIndex]);
    }

    brls::Logger::info("HistoryTab: Appended {} items, now showing {} rows", items.size(), m_itemRows.size());
}

} // namespace vitasuwayomi
