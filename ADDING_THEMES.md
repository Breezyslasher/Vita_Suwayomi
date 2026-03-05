# Adding New Themes to VitaSuwayomi

## Overview

VitaSuwayomi uses a **palette-based theme system**. Each theme is a `ThemePalette` struct containing 26 color slots. Adding a new theme requires changes in only 3 files.

---

## Step 1: Add the enum value

**File:** `include/app/application.hpp`

Add your theme to the `AppTheme` enum:

```cpp
enum class AppTheme {
    SYSTEM = 0,
    LIGHT = 1,
    DARK = 2,
    NEON_VAPORWAVE = 3,
    // ... (4-22 are built-in themes from Komikku)
    MY_NEW_THEME = 23    // <-- Add here, increment the number
};
```

---

## Step 2: Create the color palette

**File:** `src/app/application.cpp`

Find the `getPalette()` function. Add a new `ThemePalette` struct with your 16 colors:

```cpp
// My New Theme palette
static const ThemePalette myNewPalette = {
    nvgRGB(R, G, B),            // accent: Primary accent (buttons, highlights)
    nvgRGB(R, G, B),            // secondary: Secondary accent color
    nvgRGB(R, G, B),            // headerText: Section header text
    nvgRGB(R, G, B),            // subtitle: Subtitle/detail text
    nvgRGBA(R, G, B, A),        // highlight: Focus highlight (use alpha ~60-80)
    nvgRGBA(R, G, B, 255),      // sidebar: Sidebar/panel background
    nvgRGBA(R, G, B, 255),      // cardBg: Card/cell background
    nvgRGBA(R, G, B, 255),      // dialogBg: Dialog/overlay background
    nvgRGB(R, G, B),            // teal: Badge/status accent color
    nvgRGB(R, G, B),            // status: Status text color
    nvgRGB(R, G, B),            // description: Body/description text
    nvgRGB(R, G, B),            // dimText: Dimmed/secondary text
    nvgRGBA(R, G, B, 255),      // rowBg: List row background
    nvgRGBA(R, G, B, 200),      // activeRowBg: Selected/active row (use alpha ~200)
    nvgRGBA(R, G, B, 200),      // inactiveRowBg: Unselected row (use alpha ~200)
    nvgRGBA(R, G, B, 255),      // trackingBtn: Tracking button color
    nvgRGBA(R, G, B, 255),      // ctaButton: Call-to-action button
    nvgRGB(R, G, B),            // sectionHeaderBg: Section header background
    nvgRGBA(R, G, B, 255),      // readerBg: Reader background
    nvgRGBA(R, G, B, 200),      // errorOverlayBg: Error overlay (translucent)
    nvgRGBA(R, G, B, 255),      // focusedRowBg: Focused row highlight
    nvgRGBA(R, G, B, 255),      // deepBg: Very deep background
    nvgRGBA(R, G, B, 100),      // separatorColor: Separator lines
    nvgRGB(R, G, B),            // successText: Success/status text
    nvgRGB(R, G, B),            // textColor: Primary text color
    nvgRGBA(R, G, B, 255),      // buttonColor: Default button background
};
```

Then add the case to the switch in `getPalette()`:

```cpp
switch (theme) {
    case AppTheme::NEON_VAPORWAVE: return vaporwavePalette;
    case AppTheme::MY_NEW_THEME: return myNewPalette;  // <-- Add here
    default: return defaultPalette;
}
```

### In the same file, update two more things:

**Update `applyTheme()`** - set the base borealis variant (LIGHT or DARK):

```cpp
case AppTheme::MY_NEW_THEME:
    variant = brls::ThemeVariant::DARK;  // or LIGHT
    break;
```

**Update `getThemeString()`** - add the display name:

```cpp
case AppTheme::MY_NEW_THEME: return "My New Theme";
```

**Update theme validation in `loadSettings()`** - increase the max value:

```cpp
// Change > 22 to > 23 (or whatever your new max enum value is)
if (static_cast<int>(m_settings.theme) < 0 || static_cast<int>(m_settings.theme) > 23) {
```

---

## Step 3: Add to settings UI

**File:** `src/view/settings_tab.cpp`

Find the theme selector and add your theme name to the options list:

```cpp
m_themeSelector->init("Theme",
    {"System", "Light", "Dark", "Neon Vaporwave", ..., "My New Theme"},  // <-- Add here
    static_cast<int>(settings.theme),
    [this](int index) { onThemeChanged(index); });
```

---

## Color Palette Reference

Here's what each color slot is used for across the app:

| Slot | Used For | Example |
|------|----------|---------|
| `accent` | Primary buttons, active highlights | Read button, active tab |
| `secondary` | Secondary accents, success indicators | Status badges |
| `headerText` | Section header text | "Chapters", source names |
| `subtitle` | Subtitle and detail text | Author names, chapter counts |
| `highlight` | Focus highlight glow | Focused item outline |
| `sidebar` | Sidebar/panel backgrounds | Detail view main background |
| `cardBg` | Card and cell backgrounds | Manga grid cells |
| `dialogBg` | Dialog and overlay backgrounds | Settings dialogs, left panel |
| `teal` | Badge accents, status indicators | Unread badge, "Install" text |
| `status` | Status text, genre tags | "Ongoing" label, genre chips |
| `description` | Body/description text | Manga description |
| `dimText` | Dimmed/muted text | Read chapter titles, counts |
| `rowBg` | List row backgrounds | Chapter rows, history items |
| `activeRowBg` | Active/selected row highlight | Selected category, enabled toggle |
| `inactiveRowBg` | Inactive row background | Unselected category, disabled toggle |
| `trackingBtn` | Tracking button | MAL/AniList tracking button |
| `ctaButton` | Call-to-action button | Read, retry, checkmarks |
| `sectionHeaderBg` | Section header background | Extensions tab headers |
| `readerBg` | Reader background | Reader/transition bg |
| `errorOverlayBg` | Error overlay | Error dialog bg (translucent) |
| `focusedRowBg` | Focused row highlight | Hovered/focused list item |
| `deepBg` | Very deep background | Tracking search view bg |
| `separatorColor` | Separator lines | Settings dividers |
| `successText` | Success/status text | Visible status labels |
| `textColor` | Primary text color | Main text |
| `buttonColor` | Default button bg | Generic buttons |

---

## Built-in Themes (from Komikku/Tachiyomi)

The app includes 19 ported themes from Komikku: Tachiyomi (blue), Catppuccin (purple),
Nord (arctic blue), Tako (warm orange), Midnight Dusk (pink), Strawberry Daiquiri (red),
Green Apple, Lavender, Matrix (neon green), Doom (blood red), Mocha (warm brown),
Sapphire (blue), Cloudflare (orange), Teal Turquoise, Tidal Wave (ocean blue),
Yotsuba (peach), Yin Yang (white-on-dark), Monochrome (pure B&W), Cotton Candy (pink-teal).

### Default (System/Light/Dark)
- Blue accent (#64B4FF), green secondary, gray subtitles
- Standard dark backgrounds

### Neon Vaporwave
- Hot pink accent (#FF32C8), cyan secondary (#00FFFF)
- Deep purple backgrounds, neon mint highlights
- Miami/synthwave aesthetic

---

## Tips

- Use `nvgRGB(r, g, b)` for solid colors (text, accents)
- Use `nvgRGBA(r, g, b, a)` for backgrounds (a = 0-255 opacity)
- Active/inactive row backgrounds look best with alpha ~200
- The highlight color needs low alpha (~60-80) for a subtle glow
- Test your theme by selecting it in Settings > Theme
- All colors are now fully palette-driven. No more `isVaporwaveTheme()` ternaries needed.
