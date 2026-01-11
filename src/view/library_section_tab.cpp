/**
 * VitaSuwayomi - Library Section Tab implementation
 * Shows manga library content organized by categories
 */

#include "view/library_section_tab.hpp"
#include "view/manga_item_cell.hpp"
#include "view/manga_detail_view.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/async.hpp"

namespace vitasuwayomi {

LibrarySectionTab::LibrarySectionTab(int categoryId, const std::string& categoryName)
    : m_categoryId(categoryId), m_categoryName(categoryName) {

    // Create alive flag for async callback safety
    m_alive = std::make_shared<bool>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(categoryName);
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(15);
    this->addView(m_titleLabel);

    // View mode selector buttons
    m_viewModeBox = new brls::Box();
    m_viewModeBox->setAxis(brls::Axis::ROW);
    m_viewModeBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_viewModeBox->setAlignItems(brls::AlignItems::CENTER);
    m_viewModeBox->setMarginBottom(15);

    // All Manga button
    m_allBtn = new brls::Button();
    m_allBtn->setText("All");
    m_allBtn->setMarginRight(10);
    m_allBtn->registerClickAction([this](brls::View* view) {
        showAllManga();
        return true;
    });
    m_viewModeBox->addView(m_allBtn);

    // Categories button
    m_categoriesBtn = new brls::Button();
    m_categoriesBtn->setText("Categories");
    m_categoriesBtn->setMarginRight(10);
    m_categoriesBtn->registerClickAction([this](brls::View* view) {
        // Load categories if not loaded
        if (!m_categoriesLoaded) {
            loadCategories();
        }
        return true;
    });
    m_viewModeBox->addView(m_categoriesBtn);

    // Downloaded button
    m_downloadedBtn = new brls::Button();
    m_downloadedBtn->setText("Downloaded");
    m_downloadedBtn->setMarginRight(10);
    m_downloadedBtn->registerClickAction([this](brls::View* view) {
        showDownloaded();
        return true;
    });
    m_viewModeBox->addView(m_downloadedBtn);

    // Unread button
    m_unreadBtn = new brls::Button();
    m_unreadBtn->setText("Unread");
    m_unreadBtn->setMarginRight(10);
    m_unreadBtn->registerClickAction([this](brls::View* view) {
        showUnread();
        return true;
    });
    m_viewModeBox->addView(m_unreadBtn);

    // Update Library button
    m_updateBtn = new brls::Button();
    m_updateBtn->setText("Update");
    m_updateBtn->setMarginRight(10);
    m_updateBtn->registerClickAction([this](brls::View* view) {
        triggerLibraryUpdate();
        return true;
    });
    m_viewModeBox->addView(m_updateBtn);

    // Back button (hidden by default)
    m_backBtn = new brls::Button();
    m_backBtn->setText("< Back");
    m_backBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->registerClickAction([this](brls::View* view) {
        showAllManga();
        return true;
    });
    m_viewModeBox->addView(m_backBtn);

    this->addView(m_viewModeBox);

    // Content grid
    m_contentGrid = new RecyclingGrid();
    m_contentGrid->setGrow(1.0f);
    m_contentGrid->setOnMangaSelected([this](const Manga& manga) {
        onMangaSelected(manga);
    });
    this->addView(m_contentGrid);

    brls::Logger::debug("LibrarySectionTab: Created for category {} ({})", m_categoryId, m_categoryName);
    loadContent();
}

LibrarySectionTab::~LibrarySectionTab() {
    if (m_alive) {
        *m_alive = false;
    }
    brls::Logger::debug("LibrarySectionTab: Destroyed for category {}", m_categoryId);
}

void LibrarySectionTab::onFocusGained() {
    brls::Box::onFocusGained();

    if (!m_loaded) {
        loadContent();
    }
}

void LibrarySectionTab::refresh() {
    m_loaded = false;
    loadContent();
}

void LibrarySectionTab::loadContent() {
    brls::Logger::debug("LibrarySectionTab::loadContent - category: {}", m_categoryId);

    std::weak_ptr<bool> aliveWeak = m_alive;
    int categoryId = m_categoryId;

    asyncRun([this, categoryId, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;

        bool success = false;
        if (categoryId == 0) {
            // Fetch all library manga
            success = client.fetchLibraryManga(manga);
        } else {
            // Fetch manga for specific category
            success = client.fetchCategoryManga(categoryId, manga);
        }

        if (success) {
            brls::Logger::info("LibrarySectionTab: Got {} manga for category {}", manga.size(), categoryId);

            brls::sync([this, manga, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) {
                    brls::Logger::debug("LibrarySectionTab: Tab destroyed, skipping UI update");
                    return;
                }

                m_mangaList = manga;

                if (m_viewMode == LibraryViewMode::ALL_MANGA) {
                    m_contentGrid->setMangaDataSource(m_mangaList);
                }

                m_loaded = true;
            });
        } else {
            brls::Logger::error("LibrarySectionTab: Failed to load manga for category {}", categoryId);

            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                // Show offline mode with downloaded items
                m_titleLabel->setText(m_categoryName + " (Offline)");
                showDownloaded();
                m_loaded = true;
            });
        }
    });

    // Preload categories
    loadCategories();
}

void LibrarySectionTab::loadCategories() {
    if (m_categoriesLoaded) return;

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Category> categories;

        if (client.fetchCategories(categories)) {
            brls::Logger::info("LibrarySectionTab: Got {} categories", categories.size());

            brls::sync([this, categories, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_categories = categories;
                m_categoriesLoaded = true;

                // Hide categories button if no categories
                if (m_categories.empty() && m_categoriesBtn) {
                    m_categoriesBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        } else {
            brls::Logger::debug("LibrarySectionTab: No categories or failed to fetch");

            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_categoriesLoaded = true;
                if (m_categoriesBtn) {
                    m_categoriesBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        }
    });
}

void LibrarySectionTab::showAllManga() {
    m_viewMode = LibraryViewMode::ALL_MANGA;
    m_titleLabel->setText(m_categoryName);
    m_contentGrid->setMangaDataSource(m_mangaList);
    updateViewModeButtons();
}

void LibrarySectionTab::showByCategory(int categoryId) {
    m_viewMode = LibraryViewMode::BY_CATEGORY;

    // Find category name
    std::string categoryName = "Category";
    for (const auto& cat : m_categories) {
        if (cat.id == categoryId) {
            categoryName = cat.name;
            break;
        }
    }

    m_titleLabel->setText(m_categoryName + " - " + categoryName);
    m_filterTitle = categoryName;

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, categoryId, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;

        if (client.fetchCategoryManga(categoryId, manga)) {
            brls::Logger::info("LibrarySectionTab: Got {} manga for category {}", manga.size(), categoryId);

            brls::sync([this, manga, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_contentGrid->setMangaDataSource(manga);
                updateViewModeButtons();
            });
        } else {
            brls::Logger::error("LibrarySectionTab: Failed to fetch category manga");
            brls::sync([aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Failed to load category");
            });
        }
    });
}

void LibrarySectionTab::showDownloaded() {
    m_viewMode = LibraryViewMode::DOWNLOADED;
    m_titleLabel->setText(m_categoryName + " - Downloaded");

    // Filter manga that have downloaded chapters
    std::vector<Manga> downloadedManga;
    DownloadsManager& mgr = DownloadsManager::getInstance();

    for (const auto& manga : m_mangaList) {
        if (mgr.isMangaDownloaded(manga.id)) {
            Manga copy = manga;
            copy.isDownloaded = true;
            downloadedManga.push_back(copy);
        }
    }

    // Also include manga from DownloadsManager that might not be in library
    auto downloads = mgr.getDownloads();
    for (const auto& dl : downloads) {
        bool found = false;
        for (const auto& m : downloadedManga) {
            if (m.id == dl.mangaId) {
                found = true;
                break;
            }
        }
        if (!found && dl.completedChapters > 0) {
            Manga manga;
            manga.id = dl.mangaId;
            manga.title = dl.title;
            manga.author = dl.author;
            manga.isDownloaded = true;
            manga.downloadedCount = dl.completedChapters;
            downloadedManga.push_back(manga);
        }
    }

    m_contentGrid->setMangaDataSource(downloadedManga);
    updateViewModeButtons();
}

void LibrarySectionTab::showUnread() {
    m_viewMode = LibraryViewMode::UNREAD;
    m_titleLabel->setText(m_categoryName + " - Unread");

    // Filter manga with unread chapters
    std::vector<Manga> unreadManga;
    for (const auto& manga : m_mangaList) {
        if (manga.unreadCount > 0) {
            unreadManga.push_back(manga);
        }
    }

    // Sort by unread count descending
    std::sort(unreadManga.begin(), unreadManga.end(),
              [](const Manga& a, const Manga& b) {
                  return a.unreadCount > b.unreadCount;
              });

    m_contentGrid->setMangaDataSource(unreadManga);
    updateViewModeButtons();
}

void LibrarySectionTab::showReading() {
    m_viewMode = LibraryViewMode::READING;
    m_titleLabel->setText(m_categoryName + " - Reading");

    // Filter manga that are being read (have progress but not completed)
    std::vector<Manga> readingManga;
    for (const auto& manga : m_mangaList) {
        if (manga.lastChapterRead > 0 && manga.unreadCount > 0) {
            readingManga.push_back(manga);
        }
    }

    // Sort by last chapter read descending
    std::sort(readingManga.begin(), readingManga.end(),
              [](const Manga& a, const Manga& b) {
                  return a.lastChapterRead > b.lastChapterRead;
              });

    m_contentGrid->setMangaDataSource(readingManga);
    updateViewModeButtons();
}

void LibrarySectionTab::onMangaSelected(const Manga& manga) {
    brls::Logger::debug("LibrarySectionTab: Selected manga '{}' id={}", manga.title, manga.id);

    // Push manga detail view
    auto* detailView = new MangaDetailView(manga);
    brls::Application::pushActivity(new brls::Activity(detailView));
}

void LibrarySectionTab::onCategorySelected(const Category& category) {
    brls::Logger::debug("LibrarySectionTab: Selected category '{}' id={}", category.name, category.id);
    showByCategory(category.id);
}

void LibrarySectionTab::updateViewModeButtons() {
    bool inFilteredView = (m_viewMode != LibraryViewMode::ALL_MANGA);
    m_backBtn->setVisibility(inFilteredView ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    // Show/hide mode buttons based on view
    bool showModeButtons = !inFilteredView;
    if (m_allBtn) {
        m_allBtn->setVisibility(showModeButtons ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (m_categoriesBtn && !m_categories.empty()) {
        m_categoriesBtn->setVisibility(showModeButtons ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (m_downloadedBtn) {
        m_downloadedBtn->setVisibility(showModeButtons ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (m_unreadBtn) {
        m_unreadBtn->setVisibility(showModeButtons ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (m_updateBtn) {
        m_updateBtn->setVisibility(showModeButtons ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
}

void LibrarySectionTab::triggerLibraryUpdate() {
    brls::Logger::info("LibrarySectionTab: Triggering library update");
    brls::Application::notify("Updating library...");

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        bool success = false;
        if (m_categoryId == 0) {
            success = client.triggerLibraryUpdate();
        } else {
            success = client.triggerLibraryUpdate(m_categoryId);
        }

        brls::sync([success, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (success) {
                brls::Application::notify("Library update started");
            } else {
                brls::Application::notify("Failed to start library update");
            }
        });
    });
}

} // namespace vitasuwayomi
