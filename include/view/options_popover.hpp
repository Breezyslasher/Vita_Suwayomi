/**
 * VitaSuwayomi - Options Popover
 *
 * A compact, centered options panel over a dimmed scrim — a 1:1 port of the
 * "Options" context-menu design from VitaPlex (artboard "D4a"). Presentation
 * only: callers translate their actions into OptionRow entries and hand the
 * vector to OptionsPopover::show().
 */

#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

namespace vitasuwayomi {

// One selectable row in the popover.
struct OptionRow {
    // Leading icon. "download.png" / "refresh.png" / "cross.png" are drawn as
    // crisp MDI vector glyphs; any other non-empty value loads "icons/<icon>".
    std::string icon;
    std::string label;
    std::string sub;              // optional trailing mono sub-value
    bool primary = false;         // receives default focus
    bool danger  = false;         // muted label color (destructive/cancel)
    std::function<void()> action; // run when activated

    // Checkable rows toggle in place: activating them flips the leading
    // checkbox and runs `action` WITHOUT dismissing the popover (used for
    // multi-select lists such as categories). `checked` is the initial state.
    bool checkable = false;
    bool checked   = false;
};

class OptionsPopover {
public:
    // Show a centered options popover.
    //   contextLine — small line above the title (e.g. "MANGA", ""); a middot
    //                 (·) makes it gold, otherwise it renders dim.
    //   title       — bold title line.
    //   rows        — the selectable rows, top to bottom.
    //   onBack      — if set, the B button dismisses this popover and then runs
    //                 onBack (e.g. reopen the parent menu); otherwise B just
    //                 closes the popover.
    static void show(const std::string& contextLine,
                     const std::string& title,
                     std::vector<OptionRow> rows,
                     std::function<void()> onBack = nullptr);
};

} // namespace vitasuwayomi
