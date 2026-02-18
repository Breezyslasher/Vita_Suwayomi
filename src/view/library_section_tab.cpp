/**
 * VitaSuwayomi - Library Section Tab implementation
 * Shows manga library content organized by categories
 * Displays user's categories as tabs at the top for easy navigation
 */

#include "view/library_section_tab.hpp"
#include "view/manga_item_cell.hpp"
#include "view/manga_detail_view.hpp"
#include "view/tracking_search_view.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/async.hpp"
#include "utils/image_loader.hpp"
#include "utils/library_cache.hpp"
#include "view/migrate_search_view.hpp"
#include <chrono>
#include <thread>

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
    topRow->setHeight(50);

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
    m_categoryScrollContainer->setShrink(0);  // Don't shrink - allow scrolling beyond visible area
    m_categoryTabsBox->addView(m_categoryScrollContainer);

    topRow->addView(m_categoryTabsBox);

    // Button container for Sort and Update
    auto* buttonBox = new brls::Box();
    buttonBox->setAxis(brls::Axis::ROW);
    buttonBox->setAlignItems(brls::AlignItems::FLEX_END);
    buttonBox->setShrink(0.0f);  // Don't shrink buttons

    // Sort button container with Y button hint
    auto* sortContainer = new brls::Box();
    sortContainer->setAxis(brls::Axis::COLUMN);
    sortContainer->setAlignItems(brls::AlignItems::CENTER);
    sortContainer->setMarginLeft(10);

    auto* sortHintIcon = new brls::Image();
    sortHintIcon->setWidth(16);
    sortHintIcon->setHeight(16);
    sortHintIcon->setScalingType(brls::ImageScalingType::FIT);
    sortHintIcon->setImageFromFile("app0:resources/images/triangle_button.png");
    sortHintIcon->setMarginBottom(2);
    sortContainer->addView(sortHintIcon);

    m_sortBtn = new brls::Button();
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
        showSortMenu();
        return true;
    });
    sortContainer->addView(m_sortBtn);
    buttonBox->addView(sortContainer);

    // Load saved sort mode
    auto& app = Application::getInstance();
    int savedSort = app.getSettings().librarySortMode;
    m_sortMode = static_cast<LibrarySortMode>(savedSort);

    // Load saved group mode
    m_groupMode = app.getSettings().libraryGroupMode;

    // Initialize sort icon
    updateSortButtonText();

    // Update button container with Select button hint
    auto* updateContainer = new brls::Box();
    updateContainer->setAxis(brls::Axis::COLUMN);
    updateContainer->setAlignItems(brls::AlignItems::CENTER);
    updateContainer->setMarginLeft(8);

    auto* updateHintIcon = new brls::Image();
    updateHintIcon->setHeight(16);
    updateHintIcon->setScalingType(brls::ImageScalingType::FIT);
    updateHintIcon->setImageFromFile("app0:resources/images/select_button.png");
    updateHintIcon->setMarginBottom(2);
    updateContainer->addView(updateHintIcon);

    m_updateBtn = new brls::Button();
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
    updateContainer->addView(m_updateBtn);
    buttonBox->addView(updateContainer);

    topRow->addView(buttonBox);

    this->addView(topRow);

    // Content grid
    m_contentGrid = new RecyclingGrid();
    m_contentGrid->setGrow(1.0f);
    m_contentGrid->setOnItemSelected([this](const Manga& manga) {
        onMangaSelected(manga);
    });
    m_contentGrid->setOnPullToRefresh([this]() {
        triggerLibraryUpdate();
    });

    // Long-press on a book shows the context menu (same as START button)
    m_contentGrid->setOnItemLongPressed([this](const Manga& manga, int index) {
        showMangaContextMenu(manga, index);
    });

    // Auto-exit selection mode when all items are deselected (after a short delay)
    m_contentGrid->setOnSelectionChanged([this](int count) {
        if (m_selectionMode) {
            updateSelectionTitle();
            if (count == 0) {
                // Delay exit by ~2 seconds so user can re-select if they misclicked
                int generation = ++m_selectionExitGeneration;
                std::weak_ptr<bool> aliveWeak = m_alive;
                asyncRun([this, generation, aliveWeak]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    brls::sync([this, generation, aliveWeak]() {
                        auto alive = aliveWeak.lock();
                        if (!alive || !*alive) return;
                        // Only exit if no new selections happened since we scheduled this
                        if (m_selectionMode && m_selectionExitGeneration == generation &&
                            m_contentGrid && m_contentGrid->getSelectionCount() == 0) {
                            exitSelectionMode();
                        }
                    });
                });
            } else {
                // User selected something, cancel any pending auto-exit
                ++m_selectionExitGeneration;
            }
        }
    });

    // Apply library display settings from user preferences
    const auto& settings = Application::getInstance().getSettings();

    // Apply list row size first (before list mode, so it's ready when list mode is set)
    m_contentGrid->setListRowSize(static_cast<int>(settings.listRowSize));

    // Apply display mode (Grid/Compact/List)
    switch (settings.libraryDisplayMode) {
        case LibraryDisplayMode::GRID_NORMAL:
            m_contentGrid->setCompactMode(false);
            m_contentGrid->setListMode(false);
            break;
        case LibraryDisplayMode::GRID_COMPACT:
            m_contentGrid->setCompactMode(true);
            break;
        case LibraryDisplayMode::LIST:
            m_contentGrid->setListMode(true);
            break;
    }

    // Apply grid size (4/6/8 columns)
    switch (settings.libraryGridSize) {
        case LibraryGridSize::SMALL:
            m_contentGrid->setGridSize(4);  // Large covers (4 columns)
            break;
        case LibraryGridSize::MEDIUM:
            m_contentGrid->setGridSize(6);  // Medium (6 columns - default)
            break;
        case LibraryGridSize::LARGE:
            m_contentGrid->setGridSize(8);  // Small covers (8 columns)
            break;
    }

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
        // Only show context menu if a book cell is actually focused
        if (!m_contentGrid->hasCellFocus()) return true;
        int idx = m_contentGrid->getFocusedIndex();
        const Manga* manga = m_contentGrid->getItem(idx);
        if (manga) {
            showMangaContextMenu(*manga, idx);
        }
        return true;
    });

    // Register Select button to refresh/update current category
    this->registerAction("Update Category", brls::ControllerButton::BUTTON_BACK, [this](brls::View*) {
        triggerLibraryUpdate();
        return true;
    });

    // Register Y button (triangle) to show sort menu
    this->registerAction("Sort", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        showSortMenu();
        return true;
    });

    // Register X button for grouping mode menu
    this->registerAction("Grouping", brls::ControllerButton::BUTTON_X, [this](brls::View*) {
        showGroupModeMenu();
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

    // Apply grouping mode visibility
    if (m_groupMode == LibraryGroupMode::BY_CATEGORY) {
        if (m_categoryTabsBox) m_categoryTabsBox->setVisibility(brls::Visibility::VISIBLE);
    } else {
        if (m_categoryTabsBox) m_categoryTabsBox->setVisibility(brls::Visibility::GONE);
    }

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

            // Load default category from settings, or first category if not found
            int defaultId = Application::getInstance().getSettings().defaultCategoryId;
            bool foundDefault = false;
            if (defaultId != 0) {
                for (const auto& cat : m_categories) {
                    if (cat.id == defaultId) {
                        foundDefault = true;
                        break;
                    }
                }
            }
            if (foundDefault) {
                selectCategory(defaultId);
            } else if (!m_categories.empty()) {
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

                // Load default category from settings, or first category if not found
                int defaultId = Application::getInstance().getSettings().defaultCategoryId;
                bool foundDefault = false;
                if (defaultId != 0) {
                    for (const auto& cat : m_categories) {
                        if (cat.id == defaultId) {
                            foundDefault = true;
                            break;
                        }
                    }
                }
                if (foundDefault) {
                    selectCategory(defaultId);
                } else if (!m_categories.empty()) {
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
        btn->setMarginRight(8);
        btn->setWidth(80);
        btn->setHeight(35);
        btn->setCornerRadius(6);
        btn->setJustifyContent(brls::JustifyContent::CENTER);
        btn->setAlignItems(brls::AlignItems::CENTER);
        btn->setText("Library");
        btn->registerClickAction([this](brls::View* view) {
            selectCategory(0);
            return true;
        });
        m_categoryScrollContainer->addView(btn);
        m_categoryButtons.push_back(btn);
        return;
    }

    // Calculate total width needed for all buttons first
    // This ensures the scroll container has enough width for proper layout of all buttons
    float totalWidth = 5.0f; // Initial left padding
    std::vector<int> buttonWidths;
    std::vector<std::string> buttonNames;

    for (size_t i = 0; i < visibleCategories.size(); i++) {
        const auto& category = visibleCategories[i];
        std::string catName = category.name;
        if (catName.empty()) {
            catName = "Cat " + std::to_string(category.id);
        }
        if (catName.length() > 25) {
            catName = catName.substr(0, 23) + "..";
        }
        buttonNames.push_back(catName);

        int textWidth = static_cast<int>(catName.length()) * 14 + 24;
        if (textWidth < 60) textWidth = 60;
        if (textWidth > 280) textWidth = 280;
        buttonWidths.push_back(textWidth);
        totalWidth += textWidth + 8.0f; // button width + margin
    }

    // Set minimum width on scroll container to fit all buttons
    // This ensures off-screen buttons get proper layout
    m_categoryScrollContainer->setWidth(totalWidth);

    // Create a button for each category
    for (size_t i = 0; i < visibleCategories.size(); i++) {
        const auto& category = visibleCategories[i];
        auto* btn = new brls::Button();

        std::string catName = buttonNames[i];
        int textWidth = buttonWidths[i];

        btn->setMarginRight(8);
        btn->setHeight(35);
        btn->setCornerRadius(6);
        btn->setJustifyContent(brls::JustifyContent::CENTER);
        btn->setAlignItems(brls::AlignItems::CENTER);
        btn->setWidth(textWidth);

        // Set text after sizing is configured to ensure proper layout
        btn->setText(catName);

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

    // Force layout calculation for all buttons now that container has proper width
    m_categoryScrollContainer->invalidate();

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

    // Load per-category sort mode from settings
    auto& settings = Application::getInstance().getSettings();
    auto it = settings.categorySortModes.find(categoryId);
    if (it != settings.categorySortModes.end()) {
        m_sortMode = static_cast<LibrarySortMode>(it->second);
        brls::Logger::debug("LibrarySectionTab: Loaded category sort mode {} for category {}",
                           it->second, categoryId);
    } else {
        // No per-category setting, use default
        m_sortMode = LibrarySortMode::DEFAULT;
    }
    updateSortButtonText();

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

    // Check if this is a background refresh of the same category (don't cancel images)
    bool isSameCategory = (categoryId == m_cachedCategoryId);

    // Cancel pending image loads when switching categories
    // Keep memory cache for faster switching back to previous categories
    if (!isSameCategory) {
        ImageLoader::cancelAll();
        // Clear cached state when switching categories
        m_cachedMangaList.clear();
        m_cachedCategoryId = categoryId;
    }

    // Check if we have cached data (for instant loading)
    bool cacheEnabled = Application::getInstance().getSettings().cacheLibraryData;
    LibraryCache& cache = LibraryCache::getInstance();

    if (cacheEnabled && cache.hasCategoryCache(categoryId)) {
        std::vector<Manga> cachedManga;
        if (cache.loadCategoryManga(categoryId, cachedManga)) {
            brls::Logger::info("LibrarySectionTab: Loaded {} manga from cache for category {}",
                              cachedManga.size(), categoryId);
            m_fullMangaList = cachedManga;
            m_mangaList = cachedManga;
            sortMangaList();
            m_loaded = true;
            // Continue to refresh from server in background
        }
    } else if (!isSameCategory) {
        // No cache and switching categories - clear display while loading
        m_mangaList.clear();
        if (m_contentGrid) {
            m_contentGrid->setDataSource(m_mangaList);
        }
    }

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, categoryId, aliveWeak, cacheEnabled, isSameCategory]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;

        // Retry with exponential backoff (1s, 2s, 4s)
        const int maxRetries = 3;
        const int baseDelayMs = 1000;
        bool success = false;

        for (int attempt = 0; attempt < maxRetries; attempt++) {
            // Check if we're still alive before each attempt
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (attempt > 0) {
                int delayMs = baseDelayMs * (1 << (attempt - 1));  // 1s, 2s, 4s
                brls::Logger::info("LibrarySectionTab: Retry {} for category {} in {}ms",
                                  attempt, categoryId, delayMs);
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }

            manga.clear();
            success = client.fetchCategoryManga(categoryId, manga);
            if (success) break;
        }

        if (success) {
            brls::Logger::info("LibrarySectionTab: Got {} manga for category {} from server",
                              manga.size(), categoryId);

            // Save to cache if enabled
            if (cacheEnabled) {
                LibraryCache::getInstance().saveCategoryManga(categoryId, manga);
            }

            brls::sync([this, manga, categoryId, aliveWeak, isSameCategory]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) {
                    return;
                }

                // Only update if we're still on the same category
                if (m_currentCategoryId == categoryId) {
                    // Use incremental update if refreshing the same category
                    if (isSameCategory && !m_cachedMangaList.empty()) {
                        updateMangaCellsIncrementally(manga);
                    } else {
                        m_fullMangaList = manga;
                        m_mangaList = manga;
                        sortMangaList();
                    }
                }
                m_loaded = true;
            });
        } else {
            brls::Logger::error("LibrarySectionTab: Failed to load manga for category {} after {} retries",
                               categoryId, maxRetries);

            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                // Only notify if we didn't have cached data
                if (m_mangaList.empty()) {
                    brls::Application::notify("Failed to load manga - check connection");
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

    std::string categoryName = m_currentCategoryName;
    if (categoryName.empty() || m_currentCategoryId == 0) {
        categoryName = "all manga";
    }

    std::string message = "Checking for new chapters in " + categoryName + "...";
    brls::Application::notify(message);

    std::weak_ptr<bool> aliveWeak = m_alive;
    int categoryId = m_currentCategoryId;

    asyncRun([categoryId, categoryName, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        bool success = false;
        if (categoryId == 0) {
            // Update all library
            success = client.triggerLibraryUpdate();
        } else {
            // Update specific category
            success = client.triggerLibraryUpdate(categoryId);
        }

        brls::sync([success, categoryName, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (success) {
                brls::Application::notify("Update started for " + categoryName);
            } else {
                brls::Application::notify("Failed to start update");
            }
        });
    });
}

void LibrarySectionTab::sortMangaList() {
    // Preserve focus: get currently focused manga ID before sorting
    int focusedMangaId = -1;
    if (m_contentGrid) {
        int focusedIdx = m_contentGrid->getFocusedIndex();
        if (focusedIdx >= 0 && focusedIdx < static_cast<int>(m_mangaList.size())) {
            focusedMangaId = m_mangaList[focusedIdx].id;
        }
    }

    // Resolve DEFAULT to actual sort mode from settings
    LibrarySortMode effectiveMode = m_sortMode;
    if (effectiveMode == LibrarySortMode::DEFAULT) {
        int defaultSort = Application::getInstance().getSettings().defaultLibrarySortMode;
        effectiveMode = static_cast<LibrarySortMode>(defaultSort);
    }

    // Restore full list before sorting/filtering
    // This ensures switching away from DOWNLOADED_ONLY restores all books
    if (!m_fullMangaList.empty()) {
        m_mangaList = m_fullMangaList;
    }

    // Track if we're filtering items (which changes the list size)
    size_t originalSize = m_mangaList.size();
    bool isFilterOperation = false;

    // For DOWNLOADED_ONLY mode, filter out manga with no LOCAL downloads first
    if (effectiveMode == LibrarySortMode::DOWNLOADED_ONLY) {
        DownloadsManager& dm = DownloadsManager::getInstance();
        m_mangaList.erase(
            std::remove_if(m_mangaList.begin(), m_mangaList.end(),
                [&dm](const Manga& m) {
                    DownloadItem* item = dm.getMangaDownload(m.id);
                    return !item || item->completedChapters <= 0;
                }),
            m_mangaList.end());
        isFilterOperation = (m_mangaList.size() != originalSize);
    }

    switch (effectiveMode) {
        case LibrarySortMode::DEFAULT:  // Should never happen but handle gracefully
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
        case LibrarySortMode::RECENTLY_ADDED_DESC:
            std::sort(m_mangaList.begin(), m_mangaList.end(),
                [](const Manga& a, const Manga& b) {
                    // Newest first (higher timestamp = more recent)
                    if (a.inLibraryAt != b.inLibraryAt) {
                        return a.inLibraryAt > b.inLibraryAt;
                    }
                    return a.id > b.id;
                });
            break;
        case LibrarySortMode::RECENTLY_ADDED_ASC:
            std::sort(m_mangaList.begin(), m_mangaList.end(),
                [](const Manga& a, const Manga& b) {
                    // Oldest first (lower timestamp = older)
                    if (a.inLibraryAt != b.inLibraryAt) {
                        return a.inLibraryAt < b.inLibraryAt;
                    }
                    return a.id < b.id;
                });
            break;
        case LibrarySortMode::LAST_READ:
            std::sort(m_mangaList.begin(), m_mangaList.end(),
                [](const Manga& a, const Manga& b) {
                    // Most recently read first
                    if (a.lastReadAt != b.lastReadAt) {
                        return a.lastReadAt > b.lastReadAt;
                    }
                    return a.title < b.title;
                });
            break;
        case LibrarySortMode::DATE_UPDATED_DESC:
            std::sort(m_mangaList.begin(), m_mangaList.end(),
                [](const Manga& a, const Manga& b) {
                    // Most recently updated first (latest chapter upload)
                    if (a.latestChapterUploadDate != b.latestChapterUploadDate) {
                        return a.latestChapterUploadDate > b.latestChapterUploadDate;
                    }
                    return a.title < b.title;
                });
            break;
        case LibrarySortMode::DATE_UPDATED_ASC:
            std::sort(m_mangaList.begin(), m_mangaList.end(),
                [](const Manga& a, const Manga& b) {
                    // Least recently updated first
                    if (a.latestChapterUploadDate != b.latestChapterUploadDate) {
                        return a.latestChapterUploadDate < b.latestChapterUploadDate;
                    }
                    return a.title < b.title;
                });
            break;
        case LibrarySortMode::TOTAL_CHAPTERS:
            std::sort(m_mangaList.begin(), m_mangaList.end(),
                [](const Manga& a, const Manga& b) {
                    // Sort by total chapter count (most chapters first)
                    if (a.chapterCount != b.chapterCount) {
                        return a.chapterCount > b.chapterCount;
                    }
                    return a.title < b.title;
                });
            break;
        case LibrarySortMode::DOWNLOADED_ONLY:
            {
                // Sort by LOCAL downloaded chapter count (most downloaded first)
                DownloadsManager& dm = DownloadsManager::getInstance();
                std::sort(m_mangaList.begin(), m_mangaList.end(),
                    [&dm](const Manga& a, const Manga& b) {
                        DownloadItem* itemA = dm.getMangaDownload(a.id);
                        DownloadItem* itemB = dm.getMangaDownload(b.id);
                        int countA = itemA ? itemA->completedChapters : 0;
                        int countB = itemB ? itemB->completedChapters : 0;
                        if (countA != countB) {
                            return countA > countB;
                        }
                        return a.title < b.title;
                    });
            }
            break;
    }

    // Update grid display
    if (m_contentGrid) {
        // Use in-place update for pure reordering (no items removed)
        // This avoids rebuilding the entire grid which is much more efficient
        if (!isFilterOperation && m_contentGrid->getItemCount() == static_cast<int>(m_mangaList.size())) {
            m_contentGrid->updateDataOrder(m_mangaList);
        } else {
            // Items were filtered out or list size changed, need full rebuild
            m_contentGrid->setDataSource(m_mangaList);
        }

        // Handle empty state after filtering (e.g., "Downloads Only" with no local downloads)
        if (m_mangaList.empty()) {
            // Transfer focus: prefer category button, fall back to sort button
            if (m_focusGridAfterLoad && m_selectedCategoryIndex >= 0 &&
                m_selectedCategoryIndex < static_cast<int>(m_categoryButtons.size())) {
                brls::Application::giveFocus(m_categoryButtons[m_selectedCategoryIndex]);
            } else if (m_sortBtn) {
                brls::Application::giveFocus(m_sortBtn);
            }
            m_focusGridAfterLoad = false;
            return;
        }

        // Restore focus: find the manga's new index after sorting
        int newIndex = -1;
        if (focusedMangaId >= 0) {
            for (size_t i = 0; i < m_mangaList.size(); i++) {
                if (m_mangaList[i].id == focusedMangaId) {
                    newIndex = static_cast<int>(i);
                    break;
                }
            }
        }

        // If we found the manga, focus it at its new position
        // If not found (e.g., switched categories), focus first item if:
        // - The grid currently has focus (user was navigating the library), OR
        // - We just switched categories via L/R buttons (m_focusGridAfterLoad flag)
        if (newIndex >= 0) {
            m_contentGrid->focusIndex(newIndex);
        } else if (!m_mangaList.empty() && (m_contentGrid->hasCellFocus() || m_focusGridAfterLoad)) {
            // Previous focused manga not in new list, focus first item
            m_contentGrid->focusIndex(0);
        }

        // Reset the flag after handling
        m_focusGridAfterLoad = false;
    }
}

void LibrarySectionTab::cycleSortMode() {
    // Cycle through sort modes
    switch (m_sortMode) {
        case LibrarySortMode::DEFAULT:
            m_sortMode = LibrarySortMode::TITLE_ASC;
            break;
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
            m_sortMode = LibrarySortMode::RECENTLY_ADDED_DESC;
            break;
        case LibrarySortMode::RECENTLY_ADDED_DESC:
            m_sortMode = LibrarySortMode::RECENTLY_ADDED_ASC;
            break;
        case LibrarySortMode::RECENTLY_ADDED_ASC:
            m_sortMode = LibrarySortMode::LAST_READ;
            break;
        case LibrarySortMode::LAST_READ:
            m_sortMode = LibrarySortMode::DATE_UPDATED_DESC;
            break;
        case LibrarySortMode::DATE_UPDATED_DESC:
            m_sortMode = LibrarySortMode::DATE_UPDATED_ASC;
            break;
        case LibrarySortMode::DATE_UPDATED_ASC:
            m_sortMode = LibrarySortMode::TOTAL_CHAPTERS;
            break;
        case LibrarySortMode::TOTAL_CHAPTERS:
            m_sortMode = LibrarySortMode::DOWNLOADED_ONLY;
            break;
        case LibrarySortMode::DOWNLOADED_ONLY:
            m_sortMode = LibrarySortMode::DEFAULT;
            break;
    }

    updateSortButtonText();
    sortMangaList();

    // Persist per-category sort mode
    auto& app = Application::getInstance();
    if (m_sortMode == LibrarySortMode::DEFAULT) {
        // Remove per-category setting to use default
        app.getSettings().categorySortModes.erase(m_currentCategoryId);
    } else {
        // Save per-category sort mode
        app.getSettings().categorySortModes[m_currentCategoryId] = static_cast<int>(m_sortMode);
    }
    app.saveSettings();
}

void LibrarySectionTab::showSortMenu() {
    std::vector<std::string> options = {
        "Default (Settings)",
        "A-Z",
        "Z-A",
        "Most Unread",
        "Least Unread",
        "Recently Added (Newest)",
        "Recently Added (Oldest)",
        "Last Read",
        "Date Updated (Newest)",
        "Date Updated (Oldest)",
        "Total Chapters",
        "Local Downloads Only"
    };

    // Find current selection index for highlighting
    int currentIndex = 0;
    switch (m_sortMode) {
        case LibrarySortMode::DEFAULT:
            currentIndex = 0;
            break;
        case LibrarySortMode::TITLE_ASC:
            currentIndex = 1;
            break;
        case LibrarySortMode::TITLE_DESC:
            currentIndex = 2;
            break;
        case LibrarySortMode::UNREAD_DESC:
            currentIndex = 3;
            break;
        case LibrarySortMode::UNREAD_ASC:
            currentIndex = 4;
            break;
        case LibrarySortMode::RECENTLY_ADDED_DESC:
            currentIndex = 5;
            break;
        case LibrarySortMode::RECENTLY_ADDED_ASC:
            currentIndex = 6;
            break;
        case LibrarySortMode::LAST_READ:
            currentIndex = 7;
            break;
        case LibrarySortMode::DATE_UPDATED_DESC:
            currentIndex = 8;
            break;
        case LibrarySortMode::DATE_UPDATED_ASC:
            currentIndex = 9;
            break;
        case LibrarySortMode::TOTAL_CHAPTERS:
            currentIndex = 10;
            break;
        case LibrarySortMode::DOWNLOADED_ONLY:
            currentIndex = 11;
            break;
    }

    int categoryId = m_currentCategoryId;

    auto* dropdown = new brls::Dropdown(
        "Sort By",
        options,
        [this, categoryId](int selected) {
            if (selected < 0) return; // Cancelled

            // Defer action to next frame so dropdown fully closes first
            // This fixes hover/focus transfer issues
            brls::sync([this, selected, categoryId]() {
                switch (selected) {
                    case 0:
                        m_sortMode = LibrarySortMode::DEFAULT;
                        break;
                    case 1:
                        m_sortMode = LibrarySortMode::TITLE_ASC;
                        break;
                    case 2:
                        m_sortMode = LibrarySortMode::TITLE_DESC;
                        break;
                    case 3:
                        m_sortMode = LibrarySortMode::UNREAD_DESC;
                        break;
                    case 4:
                        m_sortMode = LibrarySortMode::UNREAD_ASC;
                        break;
                    case 5:
                        m_sortMode = LibrarySortMode::RECENTLY_ADDED_DESC;
                        break;
                    case 6:
                        m_sortMode = LibrarySortMode::RECENTLY_ADDED_ASC;
                        break;
                    case 7:
                        m_sortMode = LibrarySortMode::LAST_READ;
                        break;
                    case 8:
                        m_sortMode = LibrarySortMode::DATE_UPDATED_DESC;
                        break;
                    case 9:
                        m_sortMode = LibrarySortMode::DATE_UPDATED_ASC;
                        break;
                    case 10:
                        m_sortMode = LibrarySortMode::TOTAL_CHAPTERS;
                        break;
                    case 11:
                        m_sortMode = LibrarySortMode::DOWNLOADED_ONLY;
                        break;
                }

                updateSortButtonText();
                sortMangaList();

                // Persist per-category sort mode
                auto& app = Application::getInstance();
                if (m_sortMode == LibrarySortMode::DEFAULT) {
                    // Remove per-category setting to use default
                    app.getSettings().categorySortModes.erase(categoryId);
                } else {
                    // Save per-category sort mode
                    app.getSettings().categorySortModes[categoryId] = static_cast<int>(m_sortMode);
                }
                app.saveSettings();
            });
        },
        currentIndex
    );
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void LibrarySectionTab::updateSortButtonText() {
    if (!m_sortIcon) return;

    // Get effective sort mode (resolve DEFAULT to actual sort mode)
    LibrarySortMode effectiveMode = m_sortMode;
    if (effectiveMode == LibrarySortMode::DEFAULT) {
        // Use the default sort mode from settings
        int defaultSort = Application::getInstance().getSettings().defaultLibrarySortMode;
        effectiveMode = static_cast<LibrarySortMode>(defaultSort);
    }

    std::string iconPath;
    switch (effectiveMode) {
        case LibrarySortMode::TITLE_ASC:
        default:
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
        case LibrarySortMode::RECENTLY_ADDED_DESC:
            iconPath = "app0:resources/icons/sort-clock-descending.png";  // Recently added (newest)
            break;
        case LibrarySortMode::RECENTLY_ADDED_ASC:
            iconPath = "app0:resources/icons/sort-clock-ascending.png";  // Recently added (oldest)
            break;
        case LibrarySortMode::LAST_READ:
            iconPath = "app0:resources/icons/book-open-page-variant.png";  // Last read
            break;
        case LibrarySortMode::DATE_UPDATED_DESC:
            iconPath = "app0:resources/icons/sort-calendar-descending.png";  // Date updated (newest)
            break;
        case LibrarySortMode::DATE_UPDATED_ASC:
            iconPath = "app0:resources/icons/sort-calendar-ascending.png";  // Date updated (oldest)
            break;
        case LibrarySortMode::TOTAL_CHAPTERS:
            iconPath = "app0:resources/icons/book-multiple.png";  // Total chapters
            break;
        case LibrarySortMode::DOWNLOADED_ONLY:
            iconPath = "app0:resources/icons/book-arrow-down.png";  // Downloaded only
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

    // Set flag to focus grid after loading the new category
    m_focusGridAfterLoad = true;

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

    // Set flag to focus grid after loading the new category
    m_focusGridAfterLoad = true;

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
            if (buttonWidth <= 0) buttonWidth = 90.0f;
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
                        // Selection callback handles title update and auto-exit if count is 0
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
            int destCategoryId = m_categories[selected].id;
            std::string catName = m_categories[selected].name;
            int srcCategoryId = m_currentCategoryId;

            // If moving to the same category, do nothing
            if (destCategoryId == srcCategoryId) {
                brls::Application::notify("Already in " + catName);
                if (m_selectionMode) exitSelectionMode();
                return;
            }

            std::weak_ptr<bool> aliveWeak = m_alive;
            std::vector<Manga> asyncList = capturedList;
            bool cacheEnabled = Application::getInstance().getSettings().cacheLibraryData;

            brls::Application::notify("Moving to " + catName + "...");

            asyncRun([this, asyncList, destCategoryId, srcCategoryId, catName, aliveWeak, cacheEnabled]() {
                SuwayomiClient& client = SuwayomiClient::getInstance();
                int successCount = 0;
                std::vector<int> movedMangaIds;
                for (const auto& manga : asyncList) {
                    std::vector<int> catIds = {destCategoryId};
                    if (client.setMangaCategories(manga.id, catIds)) {
                        successCount++;
                        movedMangaIds.push_back(manga.id);
                    }
                }

                brls::sync([this, successCount, movedMangaIds, catName, srcCategoryId, destCategoryId, aliveWeak, cacheEnabled]() {
                    auto alive = aliveWeak.lock();
                    if (!alive || !*alive) return;

                    brls::Application::notify("Moved " + std::to_string(successCount) +
                                              " manga to " + catName);

                    if (movedMangaIds.empty()) return;

                    // Remove moved manga from current display without full refresh
                    std::set<int> idsToRemove(movedMangaIds.begin(), movedMangaIds.end());

                    // Get focus info before removal
                    int focusedIdx = m_contentGrid ? m_contentGrid->getFocusedIndex() : -1;
                    bool hadCellFocus = m_contentGrid && m_contentGrid->hasCellFocus();

                    // Remove from working lists
                    m_mangaList.erase(
                        std::remove_if(m_mangaList.begin(), m_mangaList.end(),
                            [&idsToRemove](const Manga& m) { return idsToRemove.count(m.id) > 0; }),
                        m_mangaList.end());
                    m_fullMangaList.erase(
                        std::remove_if(m_fullMangaList.begin(), m_fullMangaList.end(),
                            [&idsToRemove](const Manga& m) { return idsToRemove.count(m.id) > 0; }),
                        m_fullMangaList.end());

                    // Update grid with removed items
                    if (m_contentGrid) {
                        m_contentGrid->removeItems(movedMangaIds);
                    }

                    // Update cache for source category
                    if (cacheEnabled) {
                        LibraryCache::getInstance().saveCategoryManga(srcCategoryId, m_fullMangaList);
                        LibraryCache::getInstance().invalidateCategoryCache(destCategoryId);
                    }

                    // Update cached manga list
                    m_cachedMangaList.clear();
                    for (const auto& m : m_fullMangaList) {
                        CachedMangaItem cached;
                        cached.id = m.id;
                        cached.unreadCount = m.unreadCount;
                        cached.lastReadAt = m.lastReadAt;
                        cached.latestChapterUploadDate = m.latestChapterUploadDate;
                        cached.chapterCount = m.chapterCount;
                        m_cachedMangaList.push_back(cached);
                    }

                    // Handle focus transfer after removal
                    if (hadCellFocus && m_contentGrid) {
                        if (m_mangaList.empty()) {
                            // Grid is now empty, focus category or sort button
                            if (m_selectedCategoryIndex >= 0 &&
                                m_selectedCategoryIndex < static_cast<int>(m_categoryButtons.size())) {
                                brls::Application::giveFocus(m_categoryButtons[m_selectedCategoryIndex]);
                            } else if (m_sortBtn) {
                                brls::Application::giveFocus(m_sortBtn);
                            }
                        } else {
                            // Focus the item at the same position or the last item
                            int newIdx = std::min(focusedIdx, static_cast<int>(m_mangaList.size()) - 1);
                            if (newIdx >= 0) {
                                m_contentGrid->focusIndex(newIdx);
                            }
                        }
                    }
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
        // toggleSelection fires onSelectionChanged which calls updateSelectionTitle
    }
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

void LibrarySectionTab::downloadNextChapters(const std::vector<Manga>& mangaList, int count) {
    std::string mode;
    if (count <= 5) mode = "next5";
    else if (count <= 10) mode = "next10";
    else mode = "next25";
    downloadChapters(mangaList, mode);
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
    bool cacheEnabled = Application::getInstance().getSettings().cacheLibraryData;

    // Capture focus info before async operation
    int focusedIdx = m_contentGrid ? m_contentGrid->getFocusedIndex() : -1;
    bool hadCellFocus = m_contentGrid && m_contentGrid->hasCellFocus();

    asyncRun([this, asyncList, aliveWeak, categoryId, cacheEnabled, focusedIdx, hadCellFocus]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        int count = 0;
        std::vector<int> removedMangaIds;
        for (const auto& manga : asyncList) {
            if (client.removeMangaFromLibrary(manga.id)) {
                count++;
                removedMangaIds.push_back(manga.id);
            }
        }

        brls::sync([this, count, removedMangaIds, aliveWeak, categoryId, cacheEnabled, focusedIdx, hadCellFocus]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            brls::Application::notify("Removed " + std::to_string(count) + " manga");

            if (removedMangaIds.empty()) return;

            // Remove from current display without full refresh
            std::set<int> idsToRemove(removedMangaIds.begin(), removedMangaIds.end());

            m_mangaList.erase(
                std::remove_if(m_mangaList.begin(), m_mangaList.end(),
                    [&idsToRemove](const Manga& m) { return idsToRemove.count(m.id) > 0; }),
                m_mangaList.end());
            m_fullMangaList.erase(
                std::remove_if(m_fullMangaList.begin(), m_fullMangaList.end(),
                    [&idsToRemove](const Manga& m) { return idsToRemove.count(m.id) > 0; }),
                m_fullMangaList.end());

            // Update grid
            if (m_contentGrid) {
                m_contentGrid->removeItems(removedMangaIds);
            }

            // Update cache
            if (cacheEnabled) {
                LibraryCache::getInstance().saveCategoryManga(categoryId, m_fullMangaList);
            }

            // Update cached manga list
            m_cachedMangaList.clear();
            for (const auto& m : m_fullMangaList) {
                CachedMangaItem cached;
                cached.id = m.id;
                cached.unreadCount = m.unreadCount;
                cached.lastReadAt = m.lastReadAt;
                cached.latestChapterUploadDate = m.latestChapterUploadDate;
                cached.chapterCount = m.chapterCount;
                m_cachedMangaList.push_back(cached);
            }

            // Handle focus transfer after removal
            if (hadCellFocus && m_contentGrid) {
                if (m_mangaList.empty()) {
                    // Grid is now empty, focus category or sort button
                    if (m_selectedCategoryIndex >= 0 &&
                        m_selectedCategoryIndex < static_cast<int>(m_categoryButtons.size())) {
                        brls::Application::giveFocus(m_categoryButtons[m_selectedCategoryIndex]);
                    } else if (m_sortBtn) {
                        brls::Application::giveFocus(m_sortBtn);
                    }
                } else {
                    // Focus the item at the same position or the last item
                    int newIdx = std::min(focusedIdx, static_cast<int>(m_mangaList.size()) - 1);
                    if (newIdx >= 0) {
                        m_contentGrid->focusIndex(newIdx);
                    }
                }
            }
        });
    });
}

void LibrarySectionTab::updateMangaCellsIncrementally(const std::vector<Manga>& newManga) {
    // Compare new manga list with cached state for incremental updates
    // This avoids full page refresh when only metadata changed (like downloads tab)

    brls::Logger::debug("LibrarySectionTab: updateMangaCellsIncrementally - {} new, {} cached",
                       newManga.size(), m_cachedMangaList.size());

    // Check if structure changed (different IDs or count)
    bool structureChanged = (newManga.size() != m_cachedMangaList.size());
    if (!structureChanged) {
        for (size_t i = 0; i < newManga.size(); i++) {
            if (newManga[i].id != m_cachedMangaList[i].id) {
                structureChanged = true;
                break;
            }
        }
    }

    // Update full list
    m_fullMangaList = newManga;

    if (structureChanged) {
        brls::Logger::debug("LibrarySectionTab: Structure changed, doing full rebuild");
        // Structure changed, need full rebuild
        m_mangaList = newManga;
        sortMangaList();  // This will call setDataSource

        // Update cache
        m_cachedMangaList.clear();
        for (const auto& m : newManga) {
            CachedMangaItem cached;
            cached.id = m.id;
            cached.unreadCount = m.unreadCount;
            cached.lastReadAt = m.lastReadAt;
            cached.latestChapterUploadDate = m.latestChapterUploadDate;
            cached.chapterCount = m.chapterCount;
            m_cachedMangaList.push_back(cached);
        }
        return;
    }

    // Structure is the same - check if any metadata changed
    bool hasChanges = false;
    for (size_t i = 0; i < newManga.size(); i++) {
        const auto& newItem = newManga[i];
        const auto& cached = m_cachedMangaList[i];
        if (newItem.unreadCount != cached.unreadCount ||
            newItem.lastReadAt != cached.lastReadAt ||
            newItem.latestChapterUploadDate != cached.latestChapterUploadDate ||
            newItem.chapterCount != cached.chapterCount) {
            hasChanges = true;
            break;
        }
    }

    if (!hasChanges) {
        brls::Logger::debug("LibrarySectionTab: No changes detected, skipping update");
        return;  // Nothing changed
    }

    brls::Logger::debug("LibrarySectionTab: Metadata changed, updating cells in place");

    // Update the manga list
    m_mangaList = newManga;

    // Apply current sort mode to get the correct order
    // But use updateCellData to avoid reloading thumbnails
    LibrarySortMode effectiveMode = m_sortMode;
    if (effectiveMode == LibrarySortMode::DEFAULT) {
        int defaultSort = Application::getInstance().getSettings().defaultLibrarySortMode;
        effectiveMode = static_cast<LibrarySortMode>(defaultSort);
    }

    // For DOWNLOADED_ONLY mode, we need to re-filter
    if (effectiveMode == LibrarySortMode::DOWNLOADED_ONLY) {
        // This requires full sort since filtering changes structure
        sortMangaList();
    } else if (m_contentGrid && m_contentGrid->getItemCount() == static_cast<int>(m_mangaList.size())) {
        // Same size and not filtering, can update in place
        // Apply the sort to m_mangaList first
        switch (effectiveMode) {
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
            case LibrarySortMode::RECENTLY_ADDED_DESC:
                std::sort(m_mangaList.begin(), m_mangaList.end(),
                    [](const Manga& a, const Manga& b) { return a.inLibraryAt > b.inLibraryAt; });
                break;
            case LibrarySortMode::RECENTLY_ADDED_ASC:
                std::sort(m_mangaList.begin(), m_mangaList.end(),
                    [](const Manga& a, const Manga& b) { return a.inLibraryAt < b.inLibraryAt; });
                break;
            case LibrarySortMode::LAST_READ:
                std::sort(m_mangaList.begin(), m_mangaList.end(),
                    [](const Manga& a, const Manga& b) { return a.lastReadAt > b.lastReadAt; });
                break;
            case LibrarySortMode::DATE_UPDATED_DESC:
                std::sort(m_mangaList.begin(), m_mangaList.end(),
                    [](const Manga& a, const Manga& b) { return a.latestChapterUploadDate > b.latestChapterUploadDate; });
                break;
            case LibrarySortMode::DATE_UPDATED_ASC:
                std::sort(m_mangaList.begin(), m_mangaList.end(),
                    [](const Manga& a, const Manga& b) { return a.latestChapterUploadDate < b.latestChapterUploadDate; });
                break;
            case LibrarySortMode::TOTAL_CHAPTERS:
                std::sort(m_mangaList.begin(), m_mangaList.end(),
                    [](const Manga& a, const Manga& b) { return a.chapterCount > b.chapterCount; });
                break;
            default:
                break;
        }

        // Update cells in place without reloading thumbnails
        m_contentGrid->updateCellData(m_mangaList);
    } else {
        // Size mismatch, need full rebuild
        sortMangaList();
    }

    // Update cache
    m_cachedMangaList.clear();
    for (const auto& m : newManga) {
        CachedMangaItem cached;
        cached.id = m.id;
        cached.unreadCount = m.unreadCount;
        cached.lastReadAt = m.lastReadAt;
        cached.latestChapterUploadDate = m.latestChapterUploadDate;
        cached.chapterCount = m.chapterCount;
        m_cachedMangaList.push_back(cached);
    }
}

void LibrarySectionTab::openTracking(const Manga& manga) {
    brls::Logger::info("LibrarySectionTab: Opening tracking for manga {}", manga.id);

    // Capture manga data for async operations
    Manga capturedManga = manga;
    std::weak_ptr<bool> aliveWeak = m_alive;

    // Fetch trackers asynchronously
    asyncRun([capturedManga, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        std::vector<Tracker> trackers;
        std::vector<TrackRecord> records;

        bool trackersOk = client.fetchTrackers(trackers);
        bool recordsOk = client.fetchMangaTracking(capturedManga.id, records);

        brls::sync([capturedManga, trackers, records, trackersOk, recordsOk, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (!trackersOk) {
                brls::Application::notify("Failed to load trackers");
                return;
            }

            // Filter to show only logged-in trackers
            std::vector<Tracker> loggedInTrackers;
            for (const auto& t : trackers) {
                if (t.isLoggedIn) {
                    loggedInTrackers.push_back(t);
                }
            }

            if (loggedInTrackers.empty()) {
                brls::Application::notify("No trackers logged in. Configure in server settings.");
                return;
            }

            // Build tracker options - show existing tracking status
            std::vector<std::string> trackerOptions;
            std::vector<Tracker> capturedTrackers;
            std::vector<TrackRecord> capturedRecords;
            std::vector<bool> hasRecords;

            for (const auto& tracker : loggedInTrackers) {
                TrackRecord* existingRecord = nullptr;
                for (const auto& r : records) {
                    if (r.trackerId == tracker.id) {
                        existingRecord = const_cast<TrackRecord*>(&r);
                        break;
                    }
                }

                std::string label = tracker.name;
                if (existingRecord && existingRecord->id > 0) {
                    label += " - Tracked";
                    if (!existingRecord->title.empty()) {
                        std::string trackTitle = existingRecord->title;
                        if (trackTitle.length() > 15) {
                            trackTitle = trackTitle.substr(0, 13) + "..";
                        }
                        label += " (" + trackTitle + ")";
                    }
                } else {
                    label += " - Not tracked";
                }

                trackerOptions.push_back(label);
                capturedTrackers.push_back(tracker);
                capturedRecords.push_back(existingRecord ? *existingRecord : TrackRecord());
                hasRecords.push_back(existingRecord != nullptr && existingRecord->id > 0);
            }

            // Lambda to handle tracker action (used for both single and multi tracker cases)
            auto handleTrackerAction = [capturedManga, aliveWeak](Tracker selectedTracker, TrackRecord selectedRecord, bool hasRecord) {
                if (hasRecord) {
                    // Already tracked - show info and offer to remove
                    std::vector<std::string> options = {"View Details", "Remove Tracking"};
                    brls::Dropdown* actionDropdown = new brls::Dropdown(
                        selectedTracker.name + ": " + selectedRecord.title,
                        options,
                        [capturedManga, selectedTracker, selectedRecord, aliveWeak](int action) {
                            if (action == 0) {
                                // View details - show tracking info
                                std::string info = "Status: " + selectedRecord.status;
                                if (selectedRecord.lastChapterRead > 0) {
                                    info += "\nProgress: Ch. " + std::to_string(selectedRecord.lastChapterRead);
                                }
                                if (selectedRecord.score > 0) {
                                    info += "\nScore: " + std::to_string(selectedRecord.score);
                                }
                                brls::Application::notify(info);
                            } else if (action == 1) {
                                // Remove tracking - show options
                                std::vector<std::string> removeOptions = {
                                    "Remove from app only",
                                    "Remove from " + selectedTracker.name + " too"
                                };

                                brls::Dropdown* removeDropdown = new brls::Dropdown(
                                    "Remove Tracking",
                                    removeOptions,
                                    [recordId = selectedRecord.id, trackerNameCopy = selectedTracker.name, aliveWeak](int selected) {
                                        if (selected < 0) return;

                                        bool deleteRemote = (selected == 1);
                                        std::string message = deleteRemote
                                            ? "This will remove tracking from both the app and " + trackerNameCopy + "."
                                            : "This will only remove tracking from the app. Your entry on " + trackerNameCopy + " will remain.";

                                        // Confirmation dialog
                                        brls::Dialog* confirmDialog = new brls::Dialog(
                                            "Remove from " + trackerNameCopy + "?\n\n" + message);
                                        confirmDialog->setCancelable(false);

                                        confirmDialog->addButton("Remove", [confirmDialog, recordId, trackerNameCopy, deleteRemote, aliveWeak]() {
                                            confirmDialog->close();

                                            brls::Application::notify("Removing from " + trackerNameCopy + "...");
                                            asyncRun([recordId, trackerNameCopy, deleteRemote, aliveWeak]() {
                                                SuwayomiClient& client = SuwayomiClient::getInstance();
                                                bool success = client.unbindTracker(recordId, deleteRemote);
                                                brls::sync([success, deleteRemote, trackerNameCopy, aliveWeak]() {
                                                    auto alive = aliveWeak.lock();
                                                    if (!alive || !*alive) return;
                                                    if (success) {
                                                        std::string msg = deleteRemote
                                                            ? "Removed from " + trackerNameCopy + " and app"
                                                            : "Removed from app (kept on " + trackerNameCopy + ")";
                                                        brls::Application::notify(msg);
                                                    } else {
                                                        brls::Application::notify("Failed to remove tracking");
                                                    }
                                                });
                                            });
                                        });

                                        confirmDialog->addButton("Cancel", [confirmDialog]() {
                                            confirmDialog->close();
                                        });

                                        confirmDialog->open();
                                    }, 0);

                                brls::Application::pushActivity(new brls::Activity(removeDropdown));
                            }
                        }, 0);
                    brls::Application::pushActivity(new brls::Activity(actionDropdown));
                } else {
                    // Not tracked - open search to add
                    std::string defaultQuery = capturedManga.title;
                    Tracker tracker = selectedTracker;
                    Manga manga = capturedManga;

                    brls::Application::getImeManager()->openForText(
                        [tracker, manga, aliveWeak](std::string text) {
                            if (text.empty()) return;

                            auto alive = aliveWeak.lock();
                            if (!alive || !*alive) return;

                            brls::Application::notify("Searching " + tracker.name + "...");

                            int trackerId = tracker.id;
                            int mangaId = manga.id;
                            std::string trackerName = tracker.name;
                            std::string searchQuery = text;

                            asyncRun([trackerId, mangaId, searchQuery, trackerName, aliveWeak]() {
                                SuwayomiClient& client = SuwayomiClient::getInstance();
                                std::vector<TrackSearchResult> results;

                                if (!client.searchTracker(trackerId, searchQuery, results)) {
                                    brls::sync([trackerName]() {
                                        brls::Application::notify("Search failed for " + trackerName);
                                    });
                                    return;
                                }

                                brls::sync([trackerId, mangaId, trackerName, results, aliveWeak]() {
                                    auto alive = aliveWeak.lock();
                                    if (!alive || !*alive) return;

                                    if (results.empty()) {
                                        brls::Application::notify("No results found on " + trackerName);
                                        return;
                                    }

                                    // Show tracking search view
                                    auto* searchView = new TrackingSearchView(trackerName, trackerId, mangaId, results);
                                    brls::Application::pushActivity(new brls::Activity(searchView));
                                });
                            });
                        },
                        "Search " + tracker.name,
                        "Enter manga title to search",
                        256,
                        defaultQuery
                    );
                }
            };

            // Skip tracker selection if only one tracker is available
            if (capturedTrackers.size() == 1) {
                handleTrackerAction(capturedTrackers[0], capturedRecords[0], hasRecords[0]);
                return;
            }

            // Show tracker selection dropdown (multiple trackers)
            brls::Dropdown* dropdown = new brls::Dropdown(
                "Track: " + capturedManga.title,
                trackerOptions,
                [capturedTrackers, capturedRecords, hasRecords, handleTrackerAction](int selected) {
                    if (selected < 0 || selected >= static_cast<int>(capturedTrackers.size())) return;

                    handleTrackerAction(capturedTrackers[selected], capturedRecords[selected], hasRecords[selected]);
                }, 0);
            brls::Application::pushActivity(new brls::Activity(dropdown));
        });
    });
}

void LibrarySectionTab::setGroupMode(LibraryGroupMode mode) {
    m_groupMode = mode;
    Application::getInstance().getSettings().libraryGroupMode = mode;
    Application::getInstance().saveSettings();

    // Reload library with new grouping
    if (mode == LibraryGroupMode::BY_CATEGORY) {
        // Show category tabs
        if (m_categoryTabsBox) m_categoryTabsBox->setVisibility(brls::Visibility::VISIBLE);
        // Reload current category
        if (!m_categories.empty()) {
            selectCategory(m_currentCategoryId);
        }
    } else if (mode == LibraryGroupMode::NO_GROUPING) {
        // Hide category tabs
        if (m_categoryTabsBox) m_categoryTabsBox->setVisibility(brls::Visibility::GONE);
        // Load all manga
        loadAllManga();
    } else if (mode == LibraryGroupMode::BY_SOURCE) {
        // Hide category tabs
        if (m_categoryTabsBox) m_categoryTabsBox->setVisibility(brls::Visibility::GONE);
        // Load all manga grouped by source
        loadBySource();
    }
}

void LibrarySectionTab::loadAllManga() {
    brls::Logger::debug("LibrarySectionTab::loadAllManga - loading all manga without grouping");

    // Update title
    if (m_titleLabel) {
        m_titleLabel->setText("All Manga");
    }

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> allManga;

        // Fetch all library manga
        if (!client.fetchLibraryManga(allManga)) {
            brls::sync([aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Failed to load library");
            });
            return;
        }

        brls::sync([this, allManga, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            m_fullMangaList = allManga;
            m_mangaList = allManga;
            sortMangaList();
            m_loaded = true;
        });
    });
}

void LibrarySectionTab::loadBySource() {
    brls::Logger::debug("LibrarySectionTab::loadBySource - loading manga grouped by source");

    // Update title
    if (m_titleLabel) {
        m_titleLabel->setText("Library by Source");
    }

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> allManga;

        // Fetch all library manga
        if (!client.fetchLibraryManga(allManga)) {
            brls::sync([aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Failed to load library");
            });
            return;
        }

        // Group by source
        std::map<std::string, std::vector<Manga>> mangaBySource;
        for (const auto& manga : allManga) {
            std::string sourceName = manga.sourceName.empty() ? "Unknown" : manga.sourceName;
            mangaBySource[sourceName].push_back(manga);
        }

        // Sort sources alphabetically
        std::vector<std::pair<std::string, std::vector<Manga>>> sortedSources;
        for (auto& pair : mangaBySource) {
            sortedSources.push_back(std::move(pair));
        }
        std::sort(sortedSources.begin(), sortedSources.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        // Flatten back to a single list with source headers
        // Note: RecyclingGrid doesn't support section headers yet, so we just sort by source
        // and the manga will be grouped together visually
        std::vector<Manga> sortedBySource;
        for (const auto& sourcePair : sortedSources) {
            // Sort manga within each source
            auto sourceManga = sourcePair.second;
            std::sort(sourceManga.begin(), sourceManga.end(),
                      [](const Manga& a, const Manga& b) { return a.title < b.title; });
            sortedBySource.insert(sortedBySource.end(), sourceManga.begin(), sourceManga.end());
        }

        brls::sync([this, sortedBySource, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            m_fullMangaList = sortedBySource;
            m_mangaList = sortedBySource;
            // Don't sort again - already sorted by source
            m_loaded = true;

            if (m_contentGrid) {
                m_contentGrid->setDataSource(m_mangaList);
            }
        });
    });
}

void LibrarySectionTab::showGroupModeMenu() {
    std::vector<std::string> options = {
        "By Category",
        "By Source",
        "No Grouping (All)"
    };

    int currentMode = static_cast<int>(m_groupMode);

    brls::Dropdown* dropdown = new brls::Dropdown(
        "Library Grouping",
        options,
        [this](int selected) {
            if (selected < 0 || selected > 2) return;
            setGroupMode(static_cast<LibraryGroupMode>(selected));
        },
        currentMode
    );

    brls::Application::pushActivity(new brls::Activity(dropdown));
}

} // namespace vitasuwayomi
