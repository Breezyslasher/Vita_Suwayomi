/**
 * VitaSuwayomi platform layer — shared implementation.
 *
 * Phone detection and scale factors, ported from VitaPlex's
 * platform_common.cpp and its music player (PlayerActivity::ui / uiRow).
 * Compiled on every platform; everything defers to
 * brls::Application::contentWidth / contentHeight, which borealis updates
 * whenever the window resizes.
 */

#include "platform/platform.hpp"

#include <borealis.hpp>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace platform {

bool isPhoneScreen() {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
    const float vw = brls::Application::contentWidth;
    const float vh = brls::Application::contentHeight;
    if (vw <= 0.0f || vh <= 0.0f) return false;   // unknown: assume not
    if (vh > vw) return true;                     // portrait phone or tablet
    return vw < 600.0f;                           // short-edge handset, landscape
#else
    return false;   // PSV / PS4 / Switch / desktop
#endif
}

// The phone designs are 412 units wide against the 1280-unit canvas.
float ui(float v)    { return isPhoneScreen() ? v * (1280.0f / 412.0f) : v; }
// VitaPlex PlayerActivity::kMobileRowScale.
float uiRow(float v) { return isPhoneScreen() ? v * 2.15f : v; }

} // namespace platform
