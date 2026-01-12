/**
 * VitaSuwayomi - Library Section Tab implementation
 * Shows manga library content organized by categories
 * Displays user's categories as tabs at the top for easy navigation
 */

#include "view/library_section_tab.hpp"
#include "view/manga_item_cell.hpp"
#include "view/manga_detail_view.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/async.hpp"

namespace vitasuwayomi {

LibrarySectionTab::LibrarySectionTab() {
    // Create alive flag for async callback safety
    m_alive = std::make_shared<bool>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Library");
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(10);
    this->addView(m_titleLabel);

    // Top row with category tabs and update button
    auto* topRow = new brls::Box();
    topRow->setAxis(brls::Axis::ROW);
    topRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    topRow->setAlignItems(brls::AlignItems::CENTER);
    topRow->setMarginBottom(15);

    // Scrollable category tabs container
    m_categoryScroller = new brls::ScrollingFrame();
    m_categoryScroller->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    m_categoryScroller->setGrow(1.0f);
    m_categoryScroller->setHeight(45);

    m_categoryTabsBox = new brls::Box();
    m_categoryTabsBox->setAxis(brls::Axis::ROW);
    m_categoryTabsBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_categoryTabsBox->setAlignItems(brls::AlignItems::CENTER);

    m_categoryScroller->setContentView(m_categoryTabsBox);
    topRow->addView(m_categoryScroller);

    // Button container for Sort and Update
    auto* buttonBox = new brls::Box();
    buttonBox->setAxis(brls::Axis::ROW);
    buttonBox->setAlignItems(brls::AlignItems::CENTER);

    // Sort button
    m_sortBtn = new brls::Button();
    m_sortBtn->setText("A-Z");
    m_sortBtn->setMarginLeft(10);
    m_sortBtn->setWidth(70);
    m_sortBtn->setHeight(35);
    m_sortBtn->registerClickAction([this](brls::View* view) {
        cycleSortMode();
        return true;
    });
    buttonBox->addView(m_sortBtn);

    // Update button
    m_updateBtn = new brls::Button();
    m_updateBtn->setText("Update");
    m_updateBtn->setMarginLeft(10);
    m_updateBtn->setWidth(80);
    m_updateBtn->setHeight(35);
    m_updateBtn->registerClickAction([this](brls::View* view) {
        triggerLibraryUpdate();
        return true;
    });
    buttonBox->addView(m_updateBtn);

    topRow->addView(buttonBox);

    this->addView(topRow);

    // Content grid
    m_contentGrid = new RecyclingGrid();
    m_contentGrid->setGrow(1.0f);
    m_contentGrid->setOnItemSelected([this](const Manga& manga) {
        onMangaSelected(manga);
    });
    this->addView(m_contentGrid);

    brls::Logger::debug("LibrarySectionTab: Created");

    // Load categories first, then create tabs
    loadCategories();
}

LibrarySectionTab::~LibrarySectionTab() {
    if (m_alive) {
        *m_alive = false;
    }
    brls::Logger::debug("LibrarySectionTab: Destroyed");
}

void LibrarySectionTab::onFocusGained() {
    brls::Box::onFocusGained();

    if (!m_loaded && m_categoriesLoaded) {
        loadCategoryManga(m_currentCategoryId);
    }
}

void LibrarySectionTab::refresh() {
    m_loaded = false;
    m_categoriesLoaded = false;
    m_categories.clear();
    m_categoryButtons.clear();

    // Clear category tabs
    if (m_categoryTabsBox) {
        m_categoryTabsBox->clearViews();
    }

    loadCategories();
}

void LibrarySectionTab::loadCategories() {
    if (m_categoriesLoaded) return;

    brls::Logger::debug("LibrarySectionTab: Loading categories...");
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Category> categories;

        if (client.fetchCategories(categories)) {
            brls::Logger::info("LibrarySectionTab: Got {} categories", categories.size());

            // Sort categories by order
            std::sort(categories.begin(), categories.end(),
                      [](const Category& a, const Category& b) {
                          return a.order < b.order;
                      });

            brls::sync([this, categories, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_categories = categories;
                m_categoriesLoaded = true;
                createCategoryTabs();

                // Load the first category (or default if none)
                if (!m_categories.empty()) {
                    selectCategory(m_categories[0].id);
                } else {
                    // No categories - load all library manga
                    selectCategory(0);
                }
            });
        } else {
            brls::Logger::warning("LibrarySectionTab: Failed to fetch categories, loading all library");

            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_categoriesLoaded = true;
                createCategoryTabs();
                selectCategory(0);
            });
        }
    });
}

void LibrarySectionTab::createCategoryTabs() {
    if (!m_categoryTabsBox) return;

    // Clear existing buttons
    m_categoryTabsBox->clearViews();
    m_categoryButtons.clear();

    // Filter out empty categories (mangaCount == 0)
    // Also filter the "Default" category (id 0) if it's empty
    std::vector<Category> visibleCategories;
    for (const auto& cat : m_categories) {
        // Skip empty categories
        if (cat.mangaCount <= 0) {
            brls::Logger::debug("LibrarySectionTab: Hiding empty category '{}' (id={})",
                              cat.name, cat.id);
            continue;
        }
        visibleCategories.push_back(cat);
    }

    // If no visible categories, show a "Library" tab that loads all manga
    if (visibleCategories.empty()) {
        auto* btn = new brls::Button();
        btn->setText("Library");
        btn->setMarginRight(10);
        // Set explicit width to fit text (approx 8 pixels per char + padding)
        btn->setWidth(100);
        btn->setHeight(35);
        btn->registerClickAction([this](brls::View* view) {
            selectCategory(0);
            return true;
        });
        m_categoryTabsBox->addView(btn);
        m_categoryButtons.push_back(btn);
        return;
    }

    // Create a button for each visible category (only show name, no count)
    for (const auto& category : visibleCategories) {
        auto* btn = new brls::Button();

        // Only show category name
        btn->setText(category.name);
        btn->setMarginRight(8);

        // Calculate width based on text length (approx 9 pixels per char + 30 padding)
        int textWidth = static_cast<int>(category.name.length()) * 9 + 30;
        if (textWidth < 60) textWidth = 60;  // Minimum width
        if (textWidth > 200) textWidth = 200; // Maximum width
        btn->setWidth(textWidth);
        btn->setHeight(35);

        int catId = category.id;
        btn->registerClickAction([this, catId](brls::View* view) {
            selectCategory(catId);
            return true;
        });

        m_categoryTabsBox->addView(btn);
        m_categoryButtons.push_back(btn);
    }

    // Store visible categories for button style updates
    m_categories = visibleCategories;

    updateCategoryButtonStyles();
}

void LibrarySectionTab::selectCategory(int categoryId) {
    m_currentCategoryId = categoryId;

    // Find category name
    m_currentCategoryName = "Library";
    for (const auto& cat : m_categories) {
        if (cat.id == categoryId) {
            m_currentCategoryName = cat.name;
            break;
        }
    }

    brls::Logger::debug("LibrarySectionTab: Selected category {} ({})",
                       categoryId, m_currentCategoryName);

    updateCategoryButtonStyles();
    loadCategoryManga(categoryId);
}

void LibrarySectionTab::updateCategoryButtonStyles() {
    // Update button styles to show selected state
    for (size_t i = 0; i < m_categoryButtons.size(); i++) {
        brls::Button* btn = m_categoryButtons[i];

        // Determine if this button is selected
        bool isSelected = false;
        if (m_categories.empty()) {
            // Only one "Library" button
            isSelected = (m_currentCategoryId == 0);
        } else if (i < m_categories.size()) {
            isSelected = (m_categories[i].id == m_currentCategoryId);
        }

        // Style the button based on selection state
        if (isSelected) {
            // Highlight selected category
            btn->setBackgroundColor(nvgRGBA(80, 150, 200, 255));
        } else {
            // Normal style
            btn->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
        }
    }
}

void LibrarySectionTab::loadCategoryManga(int categoryId) {
    brls::Logger::debug("LibrarySectionTab::loadCategoryManga - category: {}", categoryId);

    // Update title
    if (m_titleLabel) {
        m_titleLabel->setText(m_currentCategoryName);
    }

    // Clear current display while loading
    m_mangaList.clear();
    if (m_contentGrid) {
        m_contentGrid->setDataSource(m_mangaList);
    }

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, categoryId, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;

        // Always fetch manga for the specific category only
        // This ensures only books from the selected category are shown
        bool success = client.fetchCategoryManga(categoryId, manga);

        if (success) {
            brls::Logger::info("LibrarySectionTab: Got {} manga for category {}",
                              manga.size(), categoryId);

            brls::sync([this, manga, categoryId, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) {
                    brls::Logger::debug("LibrarySectionTab: Tab destroyed, skipping UI update");
                    return;
                }

                // Only update if we're still on the same category
                if (m_currentCategoryId == categoryId) {
                    m_mangaList = manga;
                    // Apply current sort mode
                    sortMangaList();
                }
                m_loaded = true;
            });
        } else {
            brls::Logger::error("LibrarySectionTab: Failed to load manga for category {}",
                               categoryId);

            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                brls::Application::notify("Failed to load manga");
                m_loaded = true;
            });
        }
    });
}

void LibrarySectionTab::onMangaSelected(const Manga& manga) {
    brls::Logger::debug("LibrarySectionTab: Selected manga '{}' id={}", manga.title, manga.id);

    // Push manga detail view
    auto* detailView = new MangaDetailView(manga);
    brls::Application::pushActivity(new brls::Activity(detailView));
}

void LibrarySectionTab::triggerLibraryUpdate() {
    brls::Logger::info("LibrarySectionTab: Triggering update for category {} ({})",
                      m_currentCategoryId, m_currentCategoryName);

    std::string message = "Updating " + m_currentCategoryName + "...";
    brls::Application::notify(message);

    std::weak_ptr<bool> aliveWeak = m_alive;
    int categoryId = m_currentCategoryId;

    asyncRun([categoryId, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        bool success = false;
        if (categoryId == 0) {
            // Update all library
            success = client.triggerLibraryUpdate();
        } else {
            // Update specific category
            success = client.triggerLibraryUpdate(categoryId);
        }

        brls::sync([success, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (success) {
                brls::Application::notify("Update started");
            } else {
                brls::Application::notify("Failed to start update");
            }
        });
    });
}

void LibrarySectionTab::sortMangaList() {
    switch (m_sortMode) {
        case LibrarySortMode::TITLE_ASC:
            std::sort(m_mangaList.begin(), m_mangaList.end(),
                [](const Manga& a, const Manga& b) { return a.title < b.title; });
            break;
        case LibrarySortMode::TITLE_DESC:
            std::sort(m_mangaList.begin(), m_mangaList.end(),
                [](const Manga& a, const Manga& b) { return a.title > b.title; });
            break;
        case LibrarySortMode::UNREAD_DESC:
            std::sort(m_mangaList.begin(), m_mangaList.end(),
                [](const Manga& a, const Manga& b) { return a.unreadCount > b.unreadCount; });
            break;
        case LibrarySortMode::UNREAD_ASC:
            std::sort(m_mangaList.begin(), m_mangaList.end(),
                [](const Manga& a, const Manga& b) { return a.unreadCount < b.unreadCount; });
            break;
        case LibrarySortMode::RECENTLY_ADDED:
            std::sort(m_mangaList.begin(), m_mangaList.end(),
                [](const Manga& a, const Manga& b) { return a.id > b.id; });
            break;
    }

    // Update grid display
    if (m_contentGrid) {
        m_contentGrid->setDataSource(m_mangaList);
    }
}

void LibrarySectionTab::cycleSortMode() {
    // Cycle through sort modes
    switch (m_sortMode) {
        case LibrarySortMode::TITLE_ASC:
            m_sortMode = LibrarySortMode::TITLE_DESC;
            break;
        case LibrarySortMode::TITLE_DESC:
            m_sortMode = LibrarySortMode::UNREAD_DESC;
            break;
        case LibrarySortMode::UNREAD_DESC:
            m_sortMode = LibrarySortMode::UNREAD_ASC;
            break;
        case LibrarySortMode::UNREAD_ASC:
            m_sortMode = LibrarySortMode::RECENTLY_ADDED;
            break;
        case LibrarySortMode::RECENTLY_ADDED:
            m_sortMode = LibrarySortMode::TITLE_ASC;
            break;
    }

    updateSortButtonText();
    sortMangaList();
}

void LibrarySectionTab::updateSortButtonText() {
    if (!m_sortBtn) return;

    switch (m_sortMode) {
        case LibrarySortMode::TITLE_ASC:
            m_sortBtn->setText("A-Z");
            break;
        case LibrarySortMode::TITLE_DESC:
            m_sortBtn->setText("Z-A");
            break;
        case LibrarySortMode::UNREAD_DESC:
            m_sortBtn->setText("Unread");
            break;
        case LibrarySortMode::UNREAD_ASC:
            m_sortBtn->setText("Read");
            break;
        case LibrarySortMode::RECENTLY_ADDED:
            m_sortBtn->setText("Recent");
            break;
    }
}

} // namespace vitasuwayomi
