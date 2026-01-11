/**
 * VitaSuwayomi - Main Activity
 * Main navigation with Library, Sources, Browse, Downloads, Settings tabs
 */

#pragma once

#include <borealis.hpp>

namespace vitasuwayomi {

class MainActivity : public brls::Activity {
public:
    MainActivity();

    brls::View* createContentView() override;

    void onContentAvailable() override;

private:
    BRLS_BIND(brls::TabFrame, tabFrame, "main/tab_frame");
};

} // namespace vitasuwayomi
