/**
 * VitaABS - Main Activity
 * Main navigation with direct library tabs (Audiobooks, Podcasts), Search, Downloads, Settings
 */

#pragma once

#include <borealis.hpp>

namespace vitaabs {

class MainActivity : public brls::Activity {
public:
    MainActivity();

    brls::View* createContentView() override;

    void onContentAvailable() override;

private:
    BRLS_BIND(brls::TabFrame, tabFrame, "main/tab_frame");
};

} // namespace vitaabs
