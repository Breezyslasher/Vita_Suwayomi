/**
 * VitaSuwayomi - Options Popover
 *
 * 1:1 port of VitaPlex's "Options" context-menu design (artboard "D4a"):
 * a compact centered panel over a dimmed scrim, with a context line + title,
 * a hairline divider, and icon rows. The download / restart / close glyphs are
 * drawn as exact MDI vector paths so they stay crisp and tintable at any size.
 */

#include "view/options_popover.hpp"

namespace vitasuwayomi {

namespace {

// Palette literals scoped to this component (matches artboard "D4a"): a neutral
// sheet surface that reads as the same material as the shell, not a tint.
namespace popcol {
    inline NVGcolor panel() { return nvgRGB(50, 50, 50); }
    inline NVGcolor line()  { return nvgRGB(67, 67, 74); }
    inline NVGcolor text()  { return nvgRGB(255, 255, 255); }
    inline NVGcolor muted() { return nvgRGB(0xA8, 0xA6, 0xB4); }
    inline NVGcolor dim()   { return nvgRGB(0x80, 0x7E, 0x8C); }
    inline NVGcolor gold()  { return nvgRGB(0xE5, 0xA0, 0x0D); }
    inline NVGcolor scrim() { return nvgRGBA(10, 9, 14, 128); }
}

// Translucent host so the underlying screen shows through the scrim.
class PopoverActivity : public brls::Activity {
public:
    explicit PopoverActivity(brls::Box* content) : brls::Activity(content) {}
    bool isTranslucent() override { return true; }
};

// download / restart / close drawn as exact MDI vector paths (24x24 viewBox)
// rather than relying on a PNG — crisp at any size and tintable. nanovg fills
// with nonzero winding, so download's separate bar + arrow sub-paths render
// correctly.
enum class MdiGlyph { Download, Restart, Close };

class MdiGlyphIcon : public brls::Box {
public:
    MdiGlyphIcon(MdiGlyph g, NVGcolor color) : m_glyph(g), m_color(color) {}
    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
        brls::Box::draw(vg, x, y, w, h, style, ctx);
        const float side = (w < h) ? w : h;
        const float gx = x + (w - side) * 0.5f;
        const float gy = y + (h - side) * 0.5f;
        const float s  = side / 24.0f;
        auto X = [=](float v) { return gx + v * s; };
        auto Y = [=](float v) { return gy + v * s; };
        nvgBeginPath(vg);
        switch (m_glyph) {
            case MdiGlyph::Close:
                nvgMoveTo(vg, X(19), Y(6.41f));   nvgLineTo(vg, X(17.59f), Y(5));
                nvgLineTo(vg, X(12), Y(10.59f));  nvgLineTo(vg, X(6.41f), Y(5));
                nvgLineTo(vg, X(5), Y(6.41f));    nvgLineTo(vg, X(10.59f), Y(12));
                nvgLineTo(vg, X(5), Y(17.59f));   nvgLineTo(vg, X(6.41f), Y(19));
                nvgLineTo(vg, X(12), Y(13.41f));  nvgLineTo(vg, X(17.59f), Y(19));
                nvgLineTo(vg, X(19), Y(17.59f));  nvgLineTo(vg, X(13.41f), Y(12));
                nvgClosePath(vg);
                break;
            case MdiGlyph::Download:
                nvgMoveTo(vg, X(5), Y(20));  nvgLineTo(vg, X(19), Y(20));
                nvgLineTo(vg, X(19), Y(18)); nvgLineTo(vg, X(5), Y(18));
                nvgClosePath(vg);
                nvgMoveTo(vg, X(19), Y(9));  nvgLineTo(vg, X(15), Y(9));
                nvgLineTo(vg, X(15), Y(3));  nvgLineTo(vg, X(9), Y(3));
                nvgLineTo(vg, X(9), Y(9));   nvgLineTo(vg, X(5), Y(9));
                nvgLineTo(vg, X(12), Y(16)); nvgLineTo(vg, X(19), Y(9));
                nvgClosePath(vg);
                break;
            case MdiGlyph::Restart:
                nvgMoveTo(vg, X(12), Y(4));
                nvgBezierTo(vg, X(14.1f), Y(4),     X(16.1f), Y(4.8f),  X(17.6f), Y(6.3f));
                nvgBezierTo(vg, X(20.7f), Y(9.4f),  X(20.7f), Y(14.5f), X(17.6f), Y(17.6f));
                nvgBezierTo(vg, X(15.8f), Y(19.5f), X(13.3f), Y(20.2f), X(10.9f), Y(19.9f));
                nvgLineTo(vg, X(11.4f), Y(17.9f));
                nvgBezierTo(vg, X(13.1f), Y(18.1f), X(14.9f), Y(17.5f), X(16.2f), Y(16.2f));
                nvgBezierTo(vg, X(18.5f), Y(13.9f), X(18.5f), Y(10.1f), X(16.2f), Y(7.7f));
                nvgBezierTo(vg, X(15.1f), Y(6.6f),  X(13.5f), Y(6),     X(12),    Y(6));
                nvgLineTo(vg, X(12), Y(10.6f));
                nvgLineTo(vg, X(7),  Y(5.6f));
                nvgLineTo(vg, X(12), Y(0.6f));
                nvgClosePath(vg);
                nvgMoveTo(vg, X(6.3f), Y(17.6f));
                nvgBezierTo(vg, X(3.7f), Y(15),    X(3.3f), Y(11),    X(5.1f), Y(7.9f));
                nvgLineTo(vg, X(6.6f), Y(9.4f));
                nvgBezierTo(vg, X(5.5f), Y(11.6f), X(5.9f), Y(14.4f), X(7.8f), Y(16.2f));
                nvgBezierTo(vg, X(8.3f), Y(16.7f), X(8.9f), Y(17.1f), X(9.6f), Y(17.4f));
                nvgLineTo(vg, X(9), Y(19.4f));
                nvgBezierTo(vg, X(8), Y(19),       X(7.1f), Y(18.4f), X(6.3f), Y(17.6f));
                nvgClosePath(vg);
                break;
        }
        nvgFillColor(vg, m_color);
        nvgFill(vg);
    }
private:
    MdiGlyph m_glyph;
    NVGcolor m_color;
};

}  // namespace

void OptionsPopover::show(const std::string& contextLine,
                          const std::string& title,
                          std::vector<OptionRow> rows,
                          std::function<void()> onBack) {
    namespace pc = popcol;

    // A middot (·) in the context line marks a "gold" case (e.g. "Ch. 12 · …");
    // every other line ("MANGA", …) renders dim.
    const bool episodic = contextLine.find("\xC2\xB7") != std::string::npos;

    // ── Geometry ────────────────────────────────────────────────────────
    const float screenW = brls::Application::contentWidth;
    const float screenH = brls::Application::contentHeight;
    const float kPopoverW = 320.0f;
    const float kMargin   = 40.0f;

    // ── Scrim (full-screen, centers the panel) ──────────────────────────
    auto* scrim = new brls::Box();
    scrim->setAxis(brls::Axis::COLUMN);
    scrim->setWidthPercentage(100.0f);
    scrim->setHeightPercentage(100.0f);
    scrim->setJustifyContent(brls::JustifyContent::CENTER);
    scrim->setAlignItems(brls::AlignItems::CENTER);
    scrim->setBackgroundColor(pc::scrim());
    scrim->addGestureRecognizer(new brls::TapGestureRecognizer(scrim,
        []() { brls::Application::popActivity(); }));

    // ── Popover panel ───────────────────────────────────────────────────
    auto* panel = new brls::Box();
    panel->setAxis(brls::Axis::COLUMN);
    panel->setBackgroundColor(pc::panel());
    panel->setBorderColor(pc::line());
    panel->setBorderThickness(1.0f);
    panel->setShadowType(brls::ShadowType::GENERIC);
    panel->setPadding(8.0f, 8.0f, 8.0f, 8.0f);
    panel->setCornerRadius(14.0f);
    // Fixed 320px, clamped on screens too narrow to hold it with margins.
    float panelW = kPopoverW;
    if (panelW + 2.0f * kMargin > screenW) panelW = screenW - 2.0f * kMargin;
    panel->setWidth(panelW);

    // ── Header (context line + title) ───────────────────────────────────
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::COLUMN);
    header->setPadding(8.0f, 10.0f, 11.0f, 10.0f);

    if (!contextLine.empty()) {
        auto* ctx = new brls::Label();
        ctx->setText(contextLine);
        ctx->setFontSize(11.0f);
        ctx->setTextColor(episodic ? pc::gold() : pc::dim());
        ctx->setSingleLine(true);
        ctx->setMarginBottom(2.0f);
        header->addView(ctx);
    }
    auto* titleLabel = new brls::Label();
    titleLabel->setText(title);
    titleLabel->setFontSize(16.0f);
    titleLabel->setTextColor(pc::text());
    titleLabel->setSingleLine(true);
    header->addView(titleLabel);
    panel->addView(header);

    // borealis only supports a uniform border, so the rule under the header is
    // a separate 1px divider box rather than a per-side border.
    auto* divider = new brls::Box();
    divider->setHeight(1.0f);
    divider->setAlignSelf(brls::AlignSelf::STRETCH);
    divider->setBackgroundColor(pc::line());
    divider->setMarginBottom(6.0f);
    panel->addView(divider);

    // Only a genuinely long menu scrolls; short menus add rows straight to the
    // panel and render centered.
    brls::Box* rowsParent = panel;
    {
        const float availForRows = screenH - 2.0f * kMargin - 90.0f;
        const float wantRows = static_cast<float>(rows.size()) * 44.0f;
        if (wantRows > availForRows && availForRows > 132.0f) {
            auto* rowsBox = new brls::Box();
            rowsBox->setAxis(brls::Axis::COLUMN);
            auto* scrollFrame = new brls::ScrollingFrame();
            scrollFrame->setContentView(rowsBox);
            scrollFrame->setHeight(availForRows);
            panel->addView(scrollFrame);
            rowsParent = rowsBox;
        }
    }

    // ── Rows ────────────────────────────────────────────────────────────
    brls::View* defaultFocus = nullptr;
    brls::View* firstRow      = nullptr;
    for (auto& r : rows) {
        OptionRow row = r;  // copy into the row closure

        auto* rowBox = new brls::Box();
        rowBox->setAxis(brls::Axis::ROW);
        rowBox->setAlignItems(brls::AlignItems::CENTER);
        rowBox->setHeight(44.0f);
        rowBox->setPadding(0.0f, 12.0f, 0.0f, 12.0f);
        rowBox->setCornerRadius(9.0f);
        rowBox->setFocusable(true);
        rowBox->setHighlightCornerRadius(9.0f);

        // Leading icon. Checkable rows show a checkbox that flips in place;
        // download/restart/close are drawn as exact MDI vectors (tinted to
        // match the row); everything else uses its PNG.
        brls::View* iconView;
        brls::Image* checkImg = nullptr;   // set for checkable rows so we can swap in place
        NVGcolor iconColor = pc::text();
        if (row.checkable) {
            auto* img = new brls::Image();
            img->setImageFromRes(std::string("icons/") +
                                 (row.checked ? "checkbox_checked.png" : "checkbox.png"));
            img->setScalingType(brls::ImageScalingType::FIT);
            iconView = img;
            checkImg = img;
        } else if (row.icon == "download.png") {
            iconView = new MdiGlyphIcon(MdiGlyph::Download, iconColor);
        } else if (row.icon == "refresh.png") {
            iconView = new MdiGlyphIcon(MdiGlyph::Restart, iconColor);
        } else if (row.icon == "cross.png") {
            iconView = new MdiGlyphIcon(MdiGlyph::Close, iconColor);
        } else {
            auto* img = new brls::Image();
            if (!row.icon.empty()) img->setImageFromRes("icons/" + row.icon);
            img->setScalingType(brls::ImageScalingType::FIT);
            iconView = img;
        }
        iconView->setWidth(20.0f);
        iconView->setHeight(20.0f);
        iconView->setMarginRight(11.0f);
        rowBox->addView(iconView);

        // Label.
        auto* lbl = new brls::Label();
        lbl->setText(row.label);
        lbl->setFontSize(15.0f);
        lbl->setSingleLine(true);
        lbl->setGrow(1.0f);
        if (row.danger) lbl->setTextColor(pc::muted());
        else            lbl->setTextColor(pc::text());
        rowBox->addView(lbl);

        // Trailing mono sub-value.
        if (!row.sub.empty()) {
            auto* sub = new brls::Label();
            sub->setText(row.sub);
            sub->setFontSize(12.0f);
            sub->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
            sub->setSingleLine(true);
            sub->setMarginLeft(8.0f);
            sub->setTextColor(pc::dim());
            rowBox->addView(sub);
        }

        // Activate. Checkable rows flip their checkbox and run the action in
        // place (no dismiss). Other rows fade the popover out, then run.
        auto act = row.action;
        if (row.checkable) {
            auto checkedState = std::make_shared<bool>(row.checked);
            auto onToggle = [act, checkImg, checkedState](brls::View*) -> bool {
                *checkedState = !*checkedState;
                if (checkImg) {
                    checkImg->setImageFromRes(std::string("icons/") +
                        (*checkedState ? "checkbox_checked.png" : "checkbox.png"));
                }
                if (act) act();
                return true;
            };
            rowBox->registerClickAction(onToggle);
        } else {
            auto onActivate = [act](brls::View*) -> bool {
                brls::Application::popActivity(brls::TransitionAnimation::FADE,
                    [act]() { if (act) act(); });
                return true;
            };
            rowBox->registerClickAction(onActivate);
        }
        rowBox->addGestureRecognizer(new brls::TapGestureRecognizer(rowBox));

        rowsParent->addView(rowBox);
        if (!firstRow) firstRow = rowBox;
        if (row.primary && !defaultFocus) defaultFocus = rowBox;
    }
    if (!defaultFocus) defaultFocus = firstRow;

    scrim->addView(panel);

    // B dismisses; if a back handler was given, run it after the pop so B
    // returns to the parent menu instead of closing outright.
    scrim->registerAction("Back", brls::ControllerButton::BUTTON_B,
        [onBack](brls::View*) {
            if (onBack) {
                brls::Application::popActivity(brls::TransitionAnimation::FADE, onBack);
            } else {
                brls::Application::popActivity();
            }
            return true;
        });

    brls::Application::pushActivity(new PopoverActivity(scrim));
    if (defaultFocus) brls::Application::giveFocus(defaultFocus);
}

} // namespace vitasuwayomi
