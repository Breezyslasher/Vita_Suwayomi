/**
 * VitaSuwayomi - Library Section Tab
 * Shows manga library content organized by categories
 * Displays user's categories as tabs at the top for easy navigation
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include "app/suwayomi_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitasuwayomi {

class LibrarySectionTab : public brls::Box {
public:
    LibrarySectionTab();

    ~LibrarySectionTab() override;

    void onFocusGained() override;
    void refresh();

private:
    void loadCategories();
    void createCategoryTabs();
    void loadCategoryManga(int categoryId);
    void selectCategory(int categoryId);
    void onMangaSelected(const Manga& manga);
    void triggerLibraryUpdate();
    void updateCategoryButtonStyles();

    // Check if this tab is still valid (not destroyed)
    bool isValid() const { return m_alive && *m_alive; }

    // Currently selected category
    int m_currentCategoryId = 0;
    std::string m_currentCategoryName = "Library";

    // UI Components
    brls::Label* m_titleLabel = nullptr;

    // Category tabs row
    brls::Box* m_categoryTabsBox = nullptr;
    brls::ScrollingFrame* m_categoryScroller = nullptr;
    std::vector<brls::Button*> m_categoryButtons;

    // Update button (separate from category tabs)
    brls::Button* m_updateBtn = nullptr;

    // Main content grid
    RecyclingGrid* m_contentGrid = nullptr;

    // Data
    std::vector<Manga> m_mangaList;
    std::vector<Category> m_categories;

    bool m_loaded = false;
    bool m_categoriesLoaded = false;

    // Shared pointer to track if this object is still alive
    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
