#pragma once

#include <borealis/views/image.hpp>

// Platform-specific button icon image paths.
// PSV uses the existing icons in images/PSV/ (PlayStation style).
// Other platforms use Kenney Input icon PNGs in images/buttons/{platform}/.
// Android hides button hints entirely (touch-only UI).

#if defined(ANDROID) || defined(__ANDROID__)
  #define HIDE_BUTTON_HINTS 1
#elif defined(__PSV__) || defined(__vita__)
  #define BUTTON_ICON_DIR "images/PSV/"
#elif defined(__SWITCH__) || defined(SWITCH)
  #define BUTTON_ICON_DIR "images/buttons/switch/"
#elif defined(PS4) || defined(__PS4__)
  #define BUTTON_ICON_DIR "images/buttons/ps4/"
#else
  #define BUTTON_ICON_DIR "images/buttons/xbox/"
#endif

#ifdef HIDE_BUTTON_HINTS
  #define BUTTON_IMG(name) ""
#else
  #define BUTTON_IMG(name) RESOURCE_PREFIX BUTTON_ICON_DIR name
#endif

inline void setButtonIcon(brls::Image* img, const char* path) {
#ifndef HIDE_BUTTON_HINTS
    img->setImageFromFile(path);
#else
    img->setVisibility(brls::Visibility::GONE);
#endif
}
