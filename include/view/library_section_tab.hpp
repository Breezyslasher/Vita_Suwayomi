/**
 * VitaSuwayomi - Library Section Tab
 * Shows manga library content organized by categories
 * Supports filtering by category, viewing all manga, and downloaded only
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include "app/suwayomi_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitasuwayomi {

// View mode for the library section
enum class LibraryViewMode {
    ALL_MANGA,      // Show all manga in the library
    BY_CATEGORY,    // Show manga filtered by category
    DOWNLOADED,     // Show only downloaded manga
    UNREAD,         // Show manga with unread chapters
    READING         // Show manga currently being read
};

class LibrarySectionTab : public brls::Box {
public:
    // For library view (show all library manga or by category)
    LibrarySectionTab(int categoryId = 0, const std::string& categoryName = "Library");

    ~LibrarySectionTab() override;

    void onFocusGained() override;
    void refresh();

private:
    void loadContent();
    void loadCategories();
    void showAllManga();
    void showByCategory(int categoryId);
    void showDownloaded();
    void showUnread();
    void showReading();
    void onMangaSelected(const Manga& manga);
    void onCategorySelected(const Category& category);
    void updateViewModeButtons();
    void triggerLibraryUpdate();

    // Check if this tab is still valid (not destroyed)
    bool isValid() const { return m_alive && *m_alive; }

    int m_categoryId = 0;
    std::string m_categoryName;

    brls::Label* m_titleLabel = nullptr;

    // View mode selector buttons
    brls::Box* m_viewModeBox = nullptr;
    brls::Button* m_allBtn = nullptr;
    brls::Button* m_categoriesBtn = nullptr;
    brls::Button* m_downloadedBtn = nullptr;
    brls::Button* m_unreadBtn = nullptr;
    brls::Button* m_updateBtn = nullptr;      // Trigger library update
    brls::Button* m_backBtn = nullptr;        // Back button when in filtered view

    // Main content grid
    RecyclingGrid* m_contentGrid = nullptr;

    // Data
    std::vector<Manga> m_mangaList;
    std::vector<Category> m_categories;

    LibraryViewMode m_viewMode = LibraryViewMode::ALL_MANGA;
    std::string m_filterTitle;
    bool m_loaded = false;
    bool m_categoriesLoaded = false;

    // Shared pointer to track if this object is still alive
    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
