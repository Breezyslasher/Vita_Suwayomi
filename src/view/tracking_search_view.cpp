/**
 * VitaSuwayomi - Tracking Search View implementation
 * Visual search results for trackers with cover images and titles
 */

#include "view/tracking_search_view.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"

namespace vitasuwayomi {

// =====================
// TrackingSearchResultCell
// =====================

TrackingSearchResultCell::TrackingSearchResultCell() {
    // Card layout similar to MangaItemCell
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setFocusable(true);
    this->setCornerRadius(8);
    this->setBackgroundColor(nvgRGBA(30, 30, 30, 255));
    this->setClipsToBounds(true);

    // Cover image - fills the card
    m_coverImage = new brls::Image();
    m_coverImage->setSize(brls::Size(160, 220));
    m_coverImage->setScalingType(brls::ImageScalingType::FILL);
    m_coverImage->setCornerRadius(8);
    this->addView(m_coverImage);

    // Bottom overlay for title
    auto* titleOverlay = new brls::Box();
    titleOverlay->setAxis(brls::Axis::COLUMN);
    titleOverlay->setJustifyContent(brls::JustifyContent::FLEX_END);
    titleOverlay->setAlignItems(brls::AlignItems::STRETCH);
    titleOverlay->setBackgroundColor(nvgRGBA(0, 0, 0, 200));
    titleOverlay->setPadding(8, 8, 6, 8);
    titleOverlay->setPositionType(brls::PositionType::ABSOLUTE);
    titleOverlay->setPositionBottom(0);
    titleOverlay->setWidth(160);

    // Title label
    m_titleLabel = new brls::Label();
    m_titleLabel->setFontSize(12);
    m_titleLabel->setTextColor(nvgRGB(255, 255, 255));
    m_titleLabel->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    titleOverlay->addView(m_titleLabel);

    // Subtitle (chapter count or publishing status)
    m_subtitleLabel = new brls::Label();
    m_subtitleLabel->setFontSize(10);
    m_subtitleLabel->setTextColor(nvgRGB(180, 180, 180));
    m_subtitleLabel->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    titleOverlay->addView(m_subtitleLabel);

    this->addView(titleOverlay);
}

void TrackingSearchResultCell::setResult(const TrackSearchResult& result) {
    m_result = result;
    m_coverLoaded = false;
    updateDisplay();
    loadCoverImage();
}

void TrackingSearchResultCell::updateDisplay() {
    if (m_titleLabel) {
        std::string title = m_result.title;
        if (title.length() > 18) {
            title = title.substr(0, 16) + "..";
        }
        m_originalTitle = title;
        m_titleLabel->setText(title);
    }

    if (m_subtitleLabel) {
        std::string subtitle;
        if (m_result.totalChapters > 0) {
            subtitle = std::to_string(m_result.totalChapters) + " chapters";
        } else if (!m_result.publishingStatus.empty()) {
            subtitle = m_result.publishingStatus;
        }
        m_subtitleLabel->setText(subtitle);
    }
}

void TrackingSearchResultCell::loadCoverImage() {
    if (!m_coverImage || m_coverLoaded) return;
    m_coverLoaded = true;

    if (m_result.coverUrl.empty()) return;

    ImageLoader::loadAsync(m_result.coverUrl, nullptr, m_coverImage);
}

void TrackingSearchResultCell::onFocusGained() {
    brls::Box::onFocusGained();
    if (m_titleLabel) {
        m_titleLabel->setText(m_result.title);
    }
}

void TrackingSearchResultCell::onFocusLost() {
    brls::Box::onFocusLost();
    if (m_titleLabel) {
        m_titleLabel->setText(m_originalTitle);
    }
}

brls::View* TrackingSearchResultCell::create() {
    return new TrackingSearchResultCell();
}

// =====================
// TrackingSearchView
// =====================

TrackingSearchView::TrackingSearchView(const std::string& trackerName, int trackerId, int mangaId,
                                       const std::vector<TrackSearchResult>& results)
    : m_trackerName(trackerName), m_trackerId(trackerId), m_mangaId(mangaId), m_results(results) {
    setupUI();
    populateResults();
}

void TrackingSearchView::setupUI() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);
    this->setBackgroundColor(nvgRGBA(20, 20, 20, 255));

    // Header with title and back hint
    auto* headerBox = new brls::Box();
    headerBox->setAxis(brls::Axis::ROW);
    headerBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    headerBox->setAlignItems(brls::AlignItems::CENTER);
    headerBox->setMarginBottom(15);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Select from " + m_trackerName);
    m_titleLabel->setFontSize(24);
    m_titleLabel->setTextColor(nvgRGB(255, 255, 255));
    headerBox->addView(m_titleLabel);

    // Results count
    auto* countLabel = new brls::Label();
    countLabel->setText(std::to_string(m_results.size()) + " results");
    countLabel->setFontSize(16);
    countLabel->setTextColor(nvgRGB(150, 150, 150));
    headerBox->addView(countLabel);

    this->addView(headerBox);

    // Scrolling frame for results
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);
    m_scrollView->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    // Grid container
    m_gridBox = new brls::Box();
    m_gridBox->setAxis(brls::Axis::COLUMN);
    m_gridBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_gridBox->setAlignItems(brls::AlignItems::FLEX_START);

    m_scrollView->setContentView(m_gridBox);
    this->addView(m_scrollView);

    // Register back button to close
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
}

void TrackingSearchView::populateResults() {
    m_cells.clear();

    int itemCount = static_cast<int>(m_results.size());
    int rowCount = (itemCount + m_columns - 1) / m_columns;

    for (int row = 0; row < rowCount; row++) {
        auto* rowBox = new brls::Box();
        rowBox->setAxis(brls::Axis::ROW);
        rowBox->setJustifyContent(brls::JustifyContent::FLEX_START);
        rowBox->setAlignItems(brls::AlignItems::FLEX_START);
        rowBox->setMarginBottom(m_cellMargin);

        for (int col = 0; col < m_columns; col++) {
            int index = row * m_columns + col;
            if (index >= itemCount) break;

            auto* cell = new TrackingSearchResultCell();
            cell->setSize(brls::Size(m_cellWidth, m_cellHeight));
            cell->setMarginRight(m_cellMargin);
            cell->setResult(m_results[index]);

            // Register click action
            int64_t remoteId = m_results[index].remoteId;
            cell->registerClickAction([this, index](brls::View* view) {
                onResultSelected(m_results[index]);
                return true;
            });
            cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

            rowBox->addView(cell);
            m_cells.push_back(cell);
        }

        m_gridBox->addView(rowBox);
    }

    // Focus first cell if available
    if (!m_cells.empty()) {
        brls::Application::giveFocus(m_cells[0]);
    }
}

void TrackingSearchView::setOnResultSelected(std::function<void(const TrackSearchResult&)> callback) {
    m_onResultSelected = callback;
}

void TrackingSearchView::onResultSelected(const TrackSearchResult& result) {
    brls::Logger::info("TrackingSearchView: Selected '{}' (remoteId: {})", result.title, result.remoteId);

    if (m_onResultSelected) {
        m_onResultSelected(result);
        return;
    }

    // Default behavior: bind tracker and go back
    brls::Application::notify("Adding to " + m_trackerName + "...");

    int trackerId = m_trackerId;
    int mangaId = m_mangaId;
    int64_t remoteId = result.remoteId;
    std::string trackerName = m_trackerName;

    asyncRun([trackerId, mangaId, remoteId, trackerName]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        if (client.bindTracker(mangaId, trackerId, remoteId)) {
            brls::sync([trackerName]() {
                brls::Application::notify("Added to " + trackerName);
                brls::Application::popActivity();
            });
        } else {
            brls::sync([trackerName]() {
                brls::Application::notify("Failed to add to " + trackerName);
            });
        }
    });
}

} // namespace vitasuwayomi
