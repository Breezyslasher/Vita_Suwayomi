#pragma once

// Platform-specific button icon image paths.
// PSV uses the existing icons in images/ (PlayStation style).
// Other platforms use Kenney Input icon PNGs in images/buttons/{platform}/.
// All files share the same base names so the rest of the code is unchanged.

#if defined(__PSV__) || defined(__vita__)
  #define BUTTON_ICON_DIR "images/"
#elif defined(__SWITCH__) || defined(SWITCH)
  #define BUTTON_ICON_DIR "images/buttons/switch/"
#elif defined(PS4) || defined(__PS4__)
  #define BUTTON_ICON_DIR "images/buttons/ps4/"
#else
  // Desktop and Android default to Xbox-style icons
  #define BUTTON_ICON_DIR "images/buttons/xbox/"
#endif

#define BUTTON_IMG(name) RESOURCE_PREFIX BUTTON_ICON_DIR name
