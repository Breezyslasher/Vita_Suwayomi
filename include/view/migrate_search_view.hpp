/**
 * VitaSuwayomi - Migrate Search View
 * Full-screen search view for manga migration
 * Searches all sources and displays results grouped by source
 */

#pragma once

#include <borealis.hpp>
#include <map>
#include <memory>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

class MigrateSearchView : public brls::Box {
public:
    MigrateSearchView(const Manga& sourceManga);

private:
    void loadSourcesAndSearch();
    void filterSources(const std::vector<Source>& allSources);
    void performSearch();
    void populateResults();
    void createSourceRow(const std::string& sourceName, const std::vector<Manga>& manga);
    void onMangaSelected(const Manga& newManga);
    void performMigration(const Manga& newManga);

    Manga m_sourceManga;

    // UI
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_statusLabel = nullptr;
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_resultsBox = nullptr;

    // Data
    std::vector<Source> m_filteredSources;
    std::map<std::string, std::vector<Manga>> m_resultsBySource;

    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
