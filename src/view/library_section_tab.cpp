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
#include "utils/image_loader.hpp"
#include "utils/library_cache.hpp"

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

    // Top row with category tabs and buttons
    auto* topRow = new brls::Box();
    topRow->setAxis(brls::Axis::ROW);
    topRow->setJustifyContent(brls::JustifyContent::FLEX_START);
    topRow->setAlignItems(brls::AlignItems::CENTER);
    topRow->setMarginBottom(15);
    topRow->setHeight(45);

    // Category tabs container - outer box clips, inner box scrolls
    m_categoryTabsBox = new brls::Box();
    m_categoryTabsBox->setAxis(brls::Axis::ROW);
    m_categoryTabsBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_categoryTabsBox->setAlignItems(brls::AlignItems::CENTER);
    m_categoryTabsBox->setGrow(1.0f);
    m_categoryTabsBox->setMarginLeft(0);
    m_categoryTabsBox->setMarginRight(10);
    m_categoryTabsBox->setClipsToBounds(true);

    // Inner container that holds category buttons
    m_categoryScrollContainer = new brls::Box();
    m_categoryScrollContainer->setAxis(brls::Axis::ROW);
    m_categoryScrollContainer->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_categoryScrollContainer->setAlignItems(brls::AlignItems::CENTER);
    m_categoryScrollContainer->setPaddingLeft(5);  // Prevent first button cutoff
    m_categoryTabsBox->addView(m_categoryScrollContainer);

    topRow->addView(m_categoryTabsBox);

    // Button container for Sort and Update
    auto* buttonBox = new brls::Box();
    buttonBox->setAxis(brls::Axis::ROW);
    buttonBox->setAlignItems(brls::AlignItems::CENTER);
    buttonBox->setShrink(0.0f);  // Don't shrink buttons

    // Sort button with icon
    m_sortBtn = new brls::Button();
    m_sortBtn->setMarginLeft(10);
    m_sortBtn->setWidth(44);
    m_sortBtn->setHeight(40);
    m_sortBtn->setCornerRadius(8);
    m_sortBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_sortBtn->setAlignItems(brls::AlignItems::CENTER);

    m_sortIcon = new brls::Image();
    m_sortIcon->setWidth(24);
    m_sortIcon->setHeight(24);
    m_sortIcon->setScalingType(brls::ImageScalingType::FIT);
    m_sortBtn->addView(m_sortIcon);

    m_sortBtn->registerClickAction([this](brls::View* view) {
        cycleSortMode();
        return true;
    });
    buttonBox->addView(m_sortBtn);

    // Initialize sort icon
    updateSortButtonText();

    // Update button - bigger size
    m_updateBtn = new brls::Button();
    m_updateBtn->setText("Update");
    m_updateBtn->setMarginLeft(8);
    m_updateBtn->setWidth(100);
    m_updateBtn->setHeight(40);
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

    // Register L/R buttons to navigate between categories
    this->registerAction("Previous Category", brls::ControllerButton::BUTTON_LB, [this](brls::View*) {
        navigateToPreviousCategory();
        return true;
    });

    this->registerAction("Next Category", brls::ControllerButton::BUTTON_RB, [this](brls::View*) {
        navigateToNextCategory();
        return true;
    });

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
    if (m_categoryScrollContainer) {
        m_categoryScrollContainer->clearViews();
        m_categoryScrollContainer->setTranslationX(0);
    }
    m_selectedCategoryIndex = 0;
    m_categoryScrollOffset = 0.0f;

    loadCategories();
}

void LibrarySectionTab::loadCategories() {
    if (m_categoriesLoaded) return;

    brls::Logger::debug("LibrarySectionTab: Loading categories...");
    std::weak_ptr<bool> aliveWeak = m_alive;

    // Check if we have cached categories for instant loading
    bool cacheEnabled = Application::getInstance().getSettings().cacheLibraryData;
    LibraryCache& cache = LibraryCache::getInstance();

    if (cacheEnabled && cache.hasCategoriesCache()) {
        std::vector<Category> cachedCategories;
        if (cache.loadCategories(cachedCategories)) {
            brls::Logger::info("LibrarySectionTab: Loaded {} categories from cache", cachedCategories.size());

            // Sort by order
            std::sort(cachedCategories.begin(), cachedCategories.end(),
                      [](const Category& a, const Category& b) {
                          return a.order < b.order;
                      });

            m_categories = cachedCategories;
            m_categoriesLoaded = true;
            createCategoryTabs();

            // Load first category from cache
            if (!m_categories.empty()) {
                selectCategory(m_categories[0].id);
            } else {
                selectCategory(0);
            }
            // Continue to try refreshing from server in background
        }
    }

    asyncRun([this, aliveWeak, cacheEnabled]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Category> categories;

        if (client.fetchCategories(categories)) {
            brls::Logger::info("LibrarySectionTab: Got {} categories from server", categories.size());

            // Sort categories by order
            std::sort(categories.begin(), categories.end(),
                      [](const Category& a, const Category& b) {
                          return a.order < b.order;
                      });

            // Save to cache
            if (cacheEnabled) {
                LibraryCache::getInstance().saveCategories(categories);
            }

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
            brls::Logger::warning("LibrarySectionTab: Failed to fetch categories from server");

            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                // Only show fallback if we don't have cached data already
                if (!m_categoriesLoaded) {
                    m_categoriesLoaded = true;
                    createCategoryTabs();
                    selectCategory(0);
                }
            });
        }
    });
}

void LibrarySectionTab::createCategoryTabs() {
    if (!m_categoryScrollContainer) return;

    // Clear existing buttons
    m_categoryScrollContainer->clearViews();
    m_categoryScrollContainer->setTranslationX(0);
    m_categoryButtons.clear();
    m_selectedCategoryIndex = 0;
    m_categoryScrollOffset = 0.0f;

    // Filter out empty categories (mangaCount == 0) and hidden categories
    // Also filter the "Default" category (id 0) if it's empty
    std::vector<Category> visibleCategories;
    const auto& hiddenIds = Application::getInstance().getSettings().hiddenCategoryIds;
    for (const auto& cat : m_categories) {
        // Skip empty categories
        if (cat.mangaCount <= 0) {
            brls::Logger::debug("LibrarySectionTab: Hiding empty category '{}' (id={})",
                              cat.name, cat.id);
            continue;
        }
        // Skip hidden categories
        if (hiddenIds.find(cat.id) != hiddenIds.end()) {
            brls::Logger::debug("LibrarySectionTab: Hiding user-hidden category '{}' (id={})",
                              cat.name, cat.id);
            continue;
        }
        visibleCategories.push_back(cat);
    }

    // Store visible categories
    m_categories = visibleCategories;

    // If no visible categories, show a "Library" tab that loads all manga
    if (visibleCategories.empty()) {
        auto* btn = new brls::Button();
        btn->setText("Library");
        btn->setMarginRight(10);
        btn->setWidth(100);
        btn->setHeight(35);
        btn->registerClickAction([this](brls::View* view) {
            selectCategory(0);
            return true;
        });
        m_categoryScrollContainer->addView(btn);
        m_categoryButtons.push_back(btn);
        return;
    }

    // Create a button for each category
    for (size_t i = 0; i < visibleCategories.size(); i++) {
        const auto& category = visibleCategories[i];
        auto* btn = new brls::Button();

        // Get category name, truncate if too long
        std::string catName = category.name;
        if (catName.empty()) {
            catName = "Cat " + std::to_string(category.id);
        }
        if (catName.length() > 25) {
            catName = catName.substr(0, 23) + "..";
        }

        btn->setText(catName);
        btn->setMarginRight(8);
        btn->setHeight(35);

        // Calculate width based on text length
        int textWidth = static_cast<int>(catName.length()) * 9 + 30;
        if (textWidth < 60) textWidth = 60;
        if (textWidth > 250) textWidth = 250;
        btn->setWidth(textWidth);

        // Click handler
        int catId = category.id;
        btn->registerClickAction([this, catId](brls::View* view) {
            selectCategory(catId);
            return true;
        });

        // Focus handler - scroll to show this button
        int idx = static_cast<int>(i);
        btn->getFocusEvent()->subscribe([this, idx](brls::View* view) {
            scrollToCategoryIndex(idx);
        });

        m_categoryScrollContainer->addView(btn);
        m_categoryButtons.push_back(btn);
    }

    updateCategoryButtonStyles();
}

void LibrarySectionTab::selectCategory(int categoryId) {
    m_currentCategoryId = categoryId;

    // Find category name and index
    m_currentCategoryName = "Library";
    m_selectedCategoryIndex = 0;
    for (size_t i = 0; i < m_categories.size(); i++) {
        if (m_categories[i].id == categoryId) {
            m_currentCategoryName = m_categories[i].name;
            m_selectedCategoryIndex = static_cast<int>(i);
            break;
        }
    }

    brls::Logger::debug("LibrarySectionTab: Selected category {} ({}) at index {}",
                       categoryId, m_currentCategoryName, m_selectedCategoryIndex);

    updateCategoryButtonStyles();
    scrollToCategoryIndex(m_selectedCategoryIndex);
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

        // Style the button based on selection state (Komikku-style)
        if (isSelected) {
            // Highlight selected category with teal accent
            btn->setBackgroundColor(nvgRGBA(0, 150, 136, 255));
        } else {
            // Normal style - dark gray
            btn->setBackgroundColor(nvgRGBA(50, 50, 50, 255));
        }
    }
}

void LibrarySectionTab::updateCategoryButtonTexts() {
    // No longer used - texts are set directly in createCategoryTabs
}

void LibrarySectionTab::loadCategoryManga(int categoryId) {
    brls::Logger::debug("LibrarySectionTab::loadCategoryManga - category: {}", categoryId);

    // Update title
    if (m_titleLabel) {
        m_titleLabel->setText(m_currentCategoryName);
    }

    // Cancel pending image loads and clear memory cache to free memory
    ImageLoader::cancelAll();
    ImageLoader::clearCache();

    // Check if we have cached data (for instant loading)
    bool cacheEnabled = Application::getInstance().getSettings().cacheLibraryData;
    LibraryCache& cache = LibraryCache::getInstance();

    if (cacheEnabled && cache.hasCategoryCache(categoryId)) {
        std::vector<Manga> cachedManga;
        if (cache.loadCategoryManga(categoryId, cachedManga)) {
            brls::Logger::info("LibrarySectionTab: Loaded {} manga from cache for category {}",
                              cachedManga.size(), categoryId);
            m_mangaList = cachedManga;
            sortMangaList();
            m_loaded = true;
            // Continue to refresh from server in background
        }
    } else {
        // No cache - clear display while loading
        m_mangaList.clear();
        if (m_contentGrid) {
            m_contentGrid->setDataSource(m_mangaList);
        }
    }

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, categoryId, aliveWeak, cacheEnabled]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;

        bool success = client.fetchCategoryManga(categoryId, manga);

        if (success) {
            brls::Logger::info("LibrarySectionTab: Got {} manga for category {} from server",
                              manga.size(), categoryId);

            // Save to cache if enabled
            if (cacheEnabled) {
                LibraryCache::getInstance().saveCategoryManga(categoryId, manga);
            }

            brls::sync([this, manga, categoryId, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) {
                    return;
                }

                // Only update if we're still on the same category
                if (m_currentCategoryId == categoryId) {
                    m_mangaList = manga;
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

                // Only notify if we didn't have cached data
                if (m_mangaList.empty()) {
                    brls::Application::notify("Failed to load manga");
                }
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
    if (!m_sortIcon) return;

    std::string iconPath;
    switch (m_sortMode) {
        case LibrarySortMode::TITLE_ASC:
            iconPath = "romfs:/icons/az.png";  // A-Z
            break;
        case LibrarySortMode::TITLE_DESC:
            iconPath = "romfs:/icons/a.png";   // Z-A (reverse)
            break;
        case LibrarySortMode::UNREAD_DESC:
            iconPath = "romfs:/icons/sort-9-1.png";  // Most unread first
            break;
        case LibrarySortMode::UNREAD_ASC:
            iconPath = "romfs:/icons/sort-1-9.png";  // Least unread first
            break;
        case LibrarySortMode::RECENTLY_ADDED:
            iconPath = "romfs:/icons/history.png";   // Recent
            break;
    }
    m_sortIcon->setImageFromFile(iconPath);
}

void LibrarySectionTab::navigateToPreviousCategory() {
    if (m_categories.empty()) return;

    // Find current category index
    int currentIndex = -1;
    for (size_t i = 0; i < m_categories.size(); i++) {
        if (m_categories[i].id == m_currentCategoryId) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    // Go to previous category (wrap around)
    if (currentIndex > 0) {
        selectCategory(m_categories[currentIndex - 1].id);
    } else if (!m_categories.empty()) {
        // Wrap to last category
        selectCategory(m_categories.back().id);
    }
}

void LibrarySectionTab::navigateToNextCategory() {
    if (m_categories.empty()) return;

    // Find current category index
    int currentIndex = -1;
    for (size_t i = 0; i < m_categories.size(); i++) {
        if (m_categories[i].id == m_currentCategoryId) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    // Go to next category (wrap around)
    if (currentIndex >= 0 && currentIndex < static_cast<int>(m_categories.size()) - 1) {
        selectCategory(m_categories[currentIndex + 1].id);
    } else if (!m_categories.empty()) {
        // Wrap to first category
        selectCategory(m_categories[0].id);
    }
}

void LibrarySectionTab::scrollToCategoryIndex(int index) {
    if (!m_categoryScrollContainer || !m_categoryTabsBox) return;
    if (index < 0 || index >= static_cast<int>(m_categoryButtons.size())) return;

    // Get the visible width of the tabs container
    float visibleWidth = m_categoryTabsBox->getWidth();
    if (visibleWidth <= 0) {
        visibleWidth = 500.0f; // Fallback width
    }

    // Calculate button position (5px padding + button widths + 8px margins)
    float buttonX = 5.0f;
    float buttonWidth = 0.0f;
    for (int i = 0; i <= index; i++) {
        if (i < static_cast<int>(m_categoryButtons.size())) {
            buttonWidth = m_categoryButtons[i]->getWidth();
            if (buttonWidth <= 0) buttonWidth = 80.0f;
            if (i < index) {
                buttonX += buttonWidth + 8.0f;
            }
        }
    }

    // Check if button is visible with current scroll offset
    float buttonLeft = buttonX + m_categoryScrollOffset;
    float buttonRight = buttonLeft + buttonWidth;

    // Scroll if button is not fully visible
    if (buttonLeft < 0) {
        // Button is off the left side
        m_categoryScrollOffset = -buttonX + 5.0f;
    } else if (buttonRight > visibleWidth) {
        // Button is off the right side
        m_categoryScrollOffset = visibleWidth - buttonX - buttonWidth - 10.0f;
    }

    // Apply the scroll offset
    m_categoryScrollContainer->setTranslationX(m_categoryScrollOffset);
}

} // namespace vitasuwayomi
