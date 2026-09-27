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

    // Find mode: search every source for a bare title (a MangaBrain
    // recommendation, which exists in no source yet) and open the picked
    // result's detail view instead of migrating anything.
    explicit MigrateSearchView(const std::string& searchTitle);

    // Find mode with the search already done — the picker openTitle() falls
    // back to, so the user doesn't wait through the same search twice.
    MigrateSearchView(const std::string& searchTitle,
                      const std::map<std::string, std::vector<Manga>>& prefetched);

    /// Open a title that lives outside the user's sources (a MangaBrain
    /// recommendation) as directly as possible: the library first, then a
    /// silent search — `preferredSourceId` first — that opens the first EXACT
    /// title match straight away. Only when nothing matches exactly does the
    /// picker appear, pre-filled with what the search found. Any of `titles`
    /// (romaji / English / native) may match; the first is the search query.
    static void openTitle(std::vector<std::string> titles, int64_t preferredSourceId);

private:
    void buildUi(const std::string& heading);
    static bool sourceAllowed(const Source& src);
    void loadSourcesAndSearch();
    void filterSources(const std::vector<Source>& allSources);
    void performSearch();
    void populateResults();
    void createSourceRow(const std::string& sourceName, const std::vector<Manga>& manga);
    void onMangaSelected(const Manga& newManga);
    void performMigration(const Manga& newManga);

    Manga m_sourceManga;
    bool m_findOnly = false;   // see the title-only constructor

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
