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
#include "view/migrate_search_view.hpp"

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

    // Load saved sort mode
    auto& app = Application::getInstance();
    int savedSort = app.getSettings().librarySortMode;
    m_sortMode = static_cast<LibrarySortMode>(savedSort);

    // Initialize sort icon
    updateSortButtonText();

    // Update button with refresh icon
    m_updateBtn = new brls::Button();
    m_updateBtn->setMarginLeft(8);
    m_updateBtn->setWidth(44);
    m_updateBtn->setHeight(40);
    m_updateBtn->setCornerRadius(8);
    m_updateBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_updateBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* updateIcon = new brls::Image();
    updateIcon->setWidth(24);
    updateIcon->setHeight(24);
    updateIcon->setScalingType(brls::ImageScalingType::FIT);
    updateIcon->setImageFromFile("app0:resources/icons/refresh.png");
    m_updateBtn->addView(updateIcon);

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

    // Register Start button for context menu on focused manga
    this->registerAction("Menu", brls::ControllerButton::BUTTON_START, [this](brls::View*) {
        if (!m_contentGrid) return true;
        int idx = m_contentGrid->getFocusedIndex();
        const Manga* manga = m_contentGrid->getItem(idx);
        if (manga) {
            showMangaContextMenu(*manga, idx);
        }
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
    // Cycle through sort modes: A-Z -> Z-A -> Unread -> Read -> (loop)
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
        case LibrarySortMode::RECENTLY_ADDED:
            m_sortMode = LibrarySortMode::TITLE_ASC;
            break;
    }

    updateSortButtonText();
    sortMangaList();

    // Persist sort mode
    auto& app = Application::getInstance();
    app.getSettings().librarySortMode = static_cast<int>(m_sortMode);
    app.saveSettings();
}

void LibrarySectionTab::updateSortButtonText() {
    if (!m_sortIcon) return;

    std::string iconPath;
    switch (m_sortMode) {
        case LibrarySortMode::TITLE_ASC:
        case LibrarySortMode::RECENTLY_ADDED:  // Fallback (not used)
            iconPath = "app0:resources/icons/az.png";  // A-Z
            break;
        case LibrarySortMode::TITLE_DESC:
            iconPath = "app0:resources/icons/za.png";  // Z-A
            break;
        case LibrarySortMode::UNREAD_DESC:
            iconPath = "app0:resources/icons/sort-9-1.png";  // Most unread first
            break;
        case LibrarySortMode::UNREAD_ASC:
            iconPath = "app0:resources/icons/sort-1-9.png";  // Least unread first
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

// ============================================================================
// Context Menu & Selection Mode
// ============================================================================

void LibrarySectionTab::showMangaContextMenu(const Manga& manga, int index) {
    brls::Logger::debug("LibrarySectionTab: Context menu for '{}' (id={})", manga.title, manga.id);

    std::vector<std::string> options;
    if (m_selectionMode) {
        options = {"Select / Deselect", "Download", "Mark as Read", "Mark as Unread",
                   "Change Categories", "Remove from Library", "Cancel Selection"};
    } else {
        options = {"Select", "Download", "Track", "Mark as Read", "Mark as Unread",
                   "Change Categories", "Remove from Library", "Migrate Source"};
    }

    auto* dropdown = new brls::Dropdown(
        m_selectionMode ? "Actions (" + std::to_string(m_contentGrid->getSelectionCount()) + " selected)" : manga.title,
        options,
        [this, manga, index](int selected) {
            if (selected < 0) return; // Cancelled
            // Defer all actions to next frame so the dropdown fully pops first.
            // Without this, pushing a new activity (submenu) gets immediately
            // popped by the dropdown's own popActivity call, and UI mutations
            // while the dropdown is still tearing down can crash.
            brls::sync([this, manga, index, selected]() {
            if (m_selectionMode) {
                // Selection mode menu
                switch (selected) {
                    case 0: // Select / Deselect
                        m_contentGrid->toggleSelection(index);
                        updateSelectionTitle();
                        break;
                    case 1: { // Download
                        auto selectedManga = m_contentGrid->getSelectedManga();
                        if (selectedManga.empty()) selectedManga.push_back(manga);
                        showDownloadSubmenu(selectedManga);
                        break;
                    }
                    case 2: { // Mark as Read
                        auto selectedManga = m_contentGrid->getSelectedManga();
                        if (selectedManga.empty()) selectedManga.push_back(manga);
                        markMangaRead(selectedManga);
                        exitSelectionMode();
                        break;
                    }
                    case 3: { // Mark as Unread
                        auto selectedManga = m_contentGrid->getSelectedManga();
                        if (selectedManga.empty()) selectedManga.push_back(manga);
                        markMangaUnread(selectedManga);
                        exitSelectionMode();
                        break;
                    }
                    case 4: { // Change Categories
                        auto selectedManga = m_contentGrid->getSelectedManga();
                        if (selectedManga.empty()) selectedManga.push_back(manga);
                        showChangeCategoryDialog(selectedManga);
                        break;
                    }
                    case 5: { // Remove from Library
                        auto selectedManga = m_contentGrid->getSelectedManga();
                        if (selectedManga.empty()) selectedManga.push_back(manga);
                        removeFromLibrary(selectedManga);
                        exitSelectionMode();
                        break;
                    }
                    case 6: // Cancel Selection
                        exitSelectionMode();
                        break;
                }
            } else {
                // Normal mode menu
                switch (selected) {
                    case 0: // Select
                        enterSelectionMode(index);
                        break;
                    case 1: { // Download
                        std::vector<Manga> list = {manga};
                        showDownloadSubmenu(list);
                        break;
                    }
                    case 2: // Track
                        openTracking(manga);
                        break;
                    case 3: { // Mark as Read
                        std::vector<Manga> list = {manga};
                        markMangaRead(list);
                        break;
                    }
                    case 4: { // Mark as Unread
                        std::vector<Manga> list = {manga};
                        markMangaUnread(list);
                        break;
                    }
                    case 5: { // Change Categories
                        std::vector<Manga> list = {manga};
                        showChangeCategoryDialog(list);
                        break;
                    }
                    case 6: { // Remove from Library
                        std::vector<Manga> list = {manga};
                        removeFromLibrary(list);
                        break;
                    }
                    case 7: // Migrate Source
                        showMigrateSourceMenu(manga);
                        break;
                }
            }
            }); // end brls::sync
        }
    );
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void LibrarySectionTab::showDownloadSubmenu(const std::vector<Manga>& mangaList) {
    std::vector<std::string> options = {
        "All Chapters", "Unread Chapters", "Next Chapter",
        "Next 5 Chapters", "Next 10 Chapters", "Next 25 Chapters"
    };

    // Capture by value for async safety
    std::vector<Manga> capturedList = mangaList;

    auto* dropdown = new brls::Dropdown(
        "Download",
        options,
        [this, capturedList](int selected) {
            std::string mode;
            switch (selected) {
                case 0: mode = "all"; break;
                case 1: mode = "unread"; break;
                case 2: mode = "next1"; break;
                case 3: mode = "next5"; break;
                case 4: mode = "next10"; break;
                case 5: mode = "next25"; break;
                default: return;
            }
            downloadChapters(capturedList, mode);
            if (m_selectionMode) exitSelectionMode();
        }
    );
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void LibrarySectionTab::showChangeCategoryDialog(const std::vector<Manga>& mangaList) {
    if (m_categories.empty()) {
        brls::Application::notify("No categories available");
        return;
    }

    std::vector<std::string> catNames;
    for (const auto& cat : m_categories) {
        catNames.push_back(cat.name);
    }

    std::vector<Manga> capturedList = mangaList;

    auto* dropdown = new brls::Dropdown(
        "Move to Category",
        catNames,
        [this, capturedList](int selected) {
            if (selected < 0 || selected >= (int)m_categories.size()) return;
            int categoryId = m_categories[selected].id;
            std::string catName = m_categories[selected].name;

            std::weak_ptr<bool> aliveWeak = m_alive;
            std::vector<Manga> asyncList = capturedList;

            brls::Application::notify("Moving to " + catName + "...");

            asyncRun([asyncList, categoryId, catName, aliveWeak]() {
                SuwayomiClient& client = SuwayomiClient::getInstance();
                int successCount = 0;
                for (const auto& manga : asyncList) {
                    std::vector<int> catIds = {categoryId};
                    if (client.setMangaCategories(manga.id, catIds)) {
                        successCount++;
                    }
                }

                brls::sync([successCount, asyncList, catName, aliveWeak]() {
                    auto alive = aliveWeak.lock();
                    if (!alive || !*alive) return;
                    brls::Application::notify("Moved " + std::to_string(successCount) +
                                              " manga to " + catName);
                });
            });

            if (m_selectionMode) exitSelectionMode();
        }
    );
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void LibrarySectionTab::showMigrateSourceMenu(const Manga& manga) {
    auto* migrateView = new MigrateSearchView(manga);
    brls::Application::pushActivity(new brls::Activity(migrateView));
}

void LibrarySectionTab::enterSelectionMode(int initialIndex) {
    m_selectionMode = true;
    if (m_contentGrid) {
        m_contentGrid->setSelectionMode(true);
        m_contentGrid->toggleSelection(initialIndex);
    }
    updateSelectionTitle();
    brls::Logger::info("LibrarySectionTab: Entered selection mode");
}

void LibrarySectionTab::exitSelectionMode() {
    m_selectionMode = false;
    if (m_contentGrid) {
        m_contentGrid->setSelectionMode(false);
    }
    if (m_titleLabel) {
        m_titleLabel->setText(m_currentCategoryName);
    }
    brls::Logger::info("LibrarySectionTab: Exited selection mode");
}

void LibrarySectionTab::updateSelectionTitle() {
    if (m_titleLabel && m_contentGrid) {
        int count = m_contentGrid->getSelectionCount();
        m_titleLabel->setText(std::to_string(count) + " selected");
    }
}

// ============================================================================
// Batch Actions
// ============================================================================

void LibrarySectionTab::downloadChapters(const std::vector<Manga>& mangaList, const std::string& mode) {
    brls::Application::notify("Queuing downloads...");

    std::weak_ptr<bool> aliveWeak = m_alive;
    std::vector<Manga> asyncList = mangaList;
    std::string asyncMode = mode;

    DownloadMode downloadMode = Application::getInstance().getSettings().downloadMode;

    asyncRun([asyncList, asyncMode, aliveWeak, downloadMode]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        DownloadsManager& localMgr = DownloadsManager::getInstance();
        localMgr.init();

        int totalServerQueued = 0;
        int totalLocalQueued = 0;

        for (const auto& manga : asyncList) {
            std::vector<Chapter> chapters;
            if (!client.fetchChapters(manga.id, chapters)) continue;

            // Chapters come sorted DESC by sourceOrder, reverse for ascending
            std::reverse(chapters.begin(), chapters.end());

            std::vector<int> serverChapterIds;
            std::vector<std::pair<int, int>> localChapterPairs;

            auto collectChapter = [&](const Chapter& ch) {
                if ((downloadMode == DownloadMode::SERVER_ONLY || downloadMode == DownloadMode::BOTH) && !ch.downloaded) {
                    serverChapterIds.push_back(ch.id);
                }
                if ((downloadMode == DownloadMode::LOCAL_ONLY || downloadMode == DownloadMode::BOTH) &&
                    !localMgr.isChapterDownloaded(manga.id, ch.index)) {
                    localChapterPairs.push_back({ch.id, ch.index});
                }
            };

            if (asyncMode == "all") {
                for (const auto& ch : chapters) {
                    collectChapter(ch);
                }
            } else if (asyncMode == "unread") {
                for (const auto& ch : chapters) {
                    if (!ch.read) collectChapter(ch);
                }
            } else {
                // next N chapters - find first unread, then take N from there
                int count = 1;
                if (asyncMode == "next5") count = 5;
                else if (asyncMode == "next10") count = 10;
                else if (asyncMode == "next25") count = 25;

                int added = 0;
                for (const auto& ch : chapters) {
                    if (!ch.read) {
                        collectChapter(ch);
                        added++;
                        if (added >= count) break;
                    }
                }
            }

            if (!serverChapterIds.empty()) {
                if (client.queueChapterDownloads(serverChapterIds)) {
                    totalServerQueued += static_cast<int>(serverChapterIds.size());
                }
            }

            if (!localChapterPairs.empty()) {
                if (localMgr.queueChaptersDownload(manga.id, localChapterPairs, manga.title)) {
                    totalLocalQueued += static_cast<int>(localChapterPairs.size());
                }
            }
        }

        // Start downloads
        if (totalServerQueued > 0) {
            client.startDownloads();
        }
        if (totalLocalQueued > 0) {
            localMgr.startDownloads();
        }

        brls::sync([totalServerQueued, totalLocalQueued, downloadMode, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            std::string msg;
            if (downloadMode == DownloadMode::SERVER_ONLY) {
                msg = "Queued " + std::to_string(totalServerQueued) + " chapters to server";
            } else if (downloadMode == DownloadMode::LOCAL_ONLY) {
                msg = "Queued " + std::to_string(totalLocalQueued) + " chapters locally";
            } else {
                msg = "Queued ";
                if (totalServerQueued > 0) msg += std::to_string(totalServerQueued) + " to server";
                if (totalServerQueued > 0 && totalLocalQueued > 0) msg += ", ";
                if (totalLocalQueued > 0) msg += std::to_string(totalLocalQueued) + " locally";
            }
            brls::Application::notify(msg);
        });
    });
}

void LibrarySectionTab::markMangaRead(const std::vector<Manga>& mangaList) {
    brls::Application::notify("Marking as read...");

    std::weak_ptr<bool> aliveWeak = m_alive;
    std::vector<Manga> asyncList = mangaList;
    int categoryId = m_currentCategoryId;

    asyncRun([this, asyncList, aliveWeak, categoryId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        int count = 0;
        for (const auto& manga : asyncList) {
            if (client.markAllChaptersRead(manga.id)) count++;
        }

        brls::sync([this, count, aliveWeak, categoryId]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            brls::Application::notify("Marked " + std::to_string(count) + " manga as read");
            // Refresh the current view
            loadCategoryManga(categoryId);
        });
    });
}

void LibrarySectionTab::markMangaUnread(const std::vector<Manga>& mangaList) {
    brls::Application::notify("Marking as unread...");

    std::weak_ptr<bool> aliveWeak = m_alive;
    std::vector<Manga> asyncList = mangaList;
    int categoryId = m_currentCategoryId;

    asyncRun([this, asyncList, aliveWeak, categoryId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        int count = 0;
        for (const auto& manga : asyncList) {
            if (client.markAllChaptersUnread(manga.id)) count++;
        }

        brls::sync([this, count, aliveWeak, categoryId]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            brls::Application::notify("Marked " + std::to_string(count) + " manga as unread");
            loadCategoryManga(categoryId);
        });
    });
}

void LibrarySectionTab::removeFromLibrary(const std::vector<Manga>& mangaList) {
    brls::Application::notify("Removing from library...");

    std::weak_ptr<bool> aliveWeak = m_alive;
    std::vector<Manga> asyncList = mangaList;
    int categoryId = m_currentCategoryId;

    asyncRun([this, asyncList, aliveWeak, categoryId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        int count = 0;
        for (const auto& manga : asyncList) {
            if (client.removeMangaFromLibrary(manga.id)) count++;
        }

        brls::sync([this, count, aliveWeak, categoryId]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            brls::Application::notify("Removed " + std::to_string(count) + " manga");
            loadCategoryManga(categoryId);
        });
    });
}

void LibrarySectionTab::openTracking(const Manga& manga) {
    // Push the manga detail view which has full tracking support
    auto* detailView = new MangaDetailView(manga);
    brls::Application::pushActivity(new brls::Activity(detailView));
}

} // namespace vitasuwayomi
