/**
 * VitaSuwayomi - Tracking Search View implementation
 * List view for tracking search results with cover, title, description, and status
 */

#include "view/tracking_search_view.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"

namespace vitasuwayomi {

// =====================
// TrackingSearchResultCell
// =====================

TrackingSearchResultCell::TrackingSearchResultCell() {
    // Horizontal list item layout: [Cover] [Text content]
    this->setAxis(brls::Axis::ROW);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setFocusable(true);
    this->setCornerRadius(8);
    this->setBackgroundColor(nvgRGBA(40, 40, 40, 255));
    this->setPadding(10);
    this->setMarginBottom(10);

    // Cover image on the left
    m_coverImage = new brls::Image();
    m_coverImage->setSize(brls::Size(80, 120));
    m_coverImage->setScalingType(brls::ImageScalingType::FIT);
    m_coverImage->setCornerRadius(6);
    m_coverImage->setMarginRight(15);
    this->addView(m_coverImage);

    // Text content container (vertical stack)
    auto* textBox = new brls::Box();
    textBox->setAxis(brls::Axis::COLUMN);
    textBox->setJustifyContent(brls::JustifyContent::CENTER);
    textBox->setAlignItems(brls::AlignItems::FLEX_START);
    textBox->setGrow(1.0f);

    // Title label (main text)
    m_titleLabel = new brls::Label();
    m_titleLabel->setFontSize(18);
    m_titleLabel->setTextColor(nvgRGB(255, 255, 255));
    m_titleLabel->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    m_titleLabel->setMarginBottom(6);
    textBox->addView(m_titleLabel);

    // Description label (subtext)
    m_descriptionLabel = new brls::Label();
    m_descriptionLabel->setFontSize(13);
    m_descriptionLabel->setTextColor(nvgRGB(180, 180, 180));
    m_descriptionLabel->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    m_descriptionLabel->setMarginBottom(6);
    textBox->addView(m_descriptionLabel);

    // Status label (publishing status, chapters, type)
    m_statusLabel = new brls::Label();
    m_statusLabel->setFontSize(12);
    m_statusLabel->setTextColor(nvgRGB(130, 200, 130));
    m_statusLabel->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    textBox->addView(m_statusLabel);

    this->addView(textBox);
}

void TrackingSearchResultCell::setResult(const TrackSearchResult& result) {
    m_result = result;
    m_coverLoaded = false;
    updateDisplay();
    loadCoverImage();
}

std::string TrackingSearchResultCell::truncateText(const std::string& text, size_t maxLen) {
    if (text.length() <= maxLen) {
        return text;
    }
    return text.substr(0, maxLen - 3) + "...";
}

void TrackingSearchResultCell::updateDisplay() {
    // Title - truncate if too long
    if (m_titleLabel) {
        m_titleLabel->setText(truncateText(m_result.title, 50));
    }

    // Description - show summary, truncated
    if (m_descriptionLabel) {
        std::string desc = m_result.summary;
        // Remove newlines and extra whitespace
        for (size_t i = 0; i < desc.length(); i++) {
            if (desc[i] == '\n' || desc[i] == '\r') {
                desc[i] = ' ';
            }
        }
        // Remove consecutive spaces
        std::string cleanDesc;
        bool lastWasSpace = false;
        for (char c : desc) {
            if (c == ' ') {
                if (!lastWasSpace) {
                    cleanDesc += c;
                    lastWasSpace = true;
                }
            } else {
                cleanDesc += c;
                lastWasSpace = false;
            }
        }
        m_descriptionLabel->setText(truncateText(cleanDesc, 100));
    }

    // Status line - combine publishing status, type, and chapters
    if (m_statusLabel) {
        std::string status;

        if (!m_result.publishingStatus.empty()) {
            status = m_result.publishingStatus;
        }

        if (!m_result.publishingType.empty()) {
            if (!status.empty()) status += " | ";
            status += m_result.publishingType;
        }

        if (m_result.totalChapters > 0) {
            if (!status.empty()) status += " | ";
            status += std::to_string(m_result.totalChapters) + " chapters";
        }

        if (!m_result.startDate.empty()) {
            if (!status.empty()) status += " | ";
            status += m_result.startDate;
        }

        m_statusLabel->setText(status);
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
    // Show full title when focused
    if (m_titleLabel) {
        m_titleLabel->setText(m_result.title);
    }
    // Highlight background
    this->setBackgroundColor(nvgRGBA(60, 60, 80, 255));
}

void TrackingSearchResultCell::onFocusLost() {
    brls::Box::onFocusLost();
    // Truncate title when not focused
    if (m_titleLabel) {
        m_titleLabel->setText(truncateText(m_result.title, 50));
    }
    // Reset background
    this->setBackgroundColor(nvgRGBA(40, 40, 40, 255));
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

    // Header with title and result count
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

    // List container (vertical)
    m_listBox = new brls::Box();
    m_listBox->setAxis(brls::Axis::COLUMN);
    m_listBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_listBox->setAlignItems(brls::AlignItems::STRETCH);

    m_scrollView->setContentView(m_listBox);
    this->addView(m_scrollView);

    // Register back button to close
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
}

void TrackingSearchView::populateResults() {
    m_cells.clear();

    for (size_t i = 0; i < m_results.size(); i++) {
        auto* cell = new TrackingSearchResultCell();
        cell->setHeight(m_itemHeight);
        cell->setResult(m_results[i]);

        // Register click action
        cell->registerClickAction([this, i](brls::View* view) {
            onResultSelected(m_results[i]);
            return true;
        });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

        m_listBox->addView(cell);
        m_cells.push_back(cell);
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
