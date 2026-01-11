/**
 * VitaSuwayomi - Recycling Grid
 * Efficient grid view for displaying manga items
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include <functional>

namespace vitasuwayomi {

class RecyclingGrid : public brls::ScrollingFrame {
public:
    RecyclingGrid();

    void setDataSource(const std::vector<Manga>& items);
    void setOnItemSelected(std::function<void(const Manga&)> callback);
    void clearViews();

    static brls::View* create();

private:
    void rebuildGrid();
    void onItemClicked(int index);

    std::vector<Manga> m_items;
    std::function<void(const Manga&)> m_onItemSelected;

    brls::Box* m_contentBox = nullptr;
    int m_columns = 4;
    int m_visibleRows = 3;
};

} // namespace vitasuwayomi
