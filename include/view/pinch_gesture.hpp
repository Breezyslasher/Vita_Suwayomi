/**
 * VitaSuwayomi - Pinch Gesture Recognizer
 * Detects two-finger pinch-to-zoom on the PS Vita front touchscreen.
 *
 * Borealis only exposes single-touch data, so this recognizer polls
 * sceTouchPeek() directly to read both fingers.  It fires its callback
 * continuously during the STAY phase so the zoom updates live.
 */

#pragma once

#include <borealis.hpp>
#include <functional>

#ifdef __vita__
#include <psp2/touch.h>
#endif

namespace vitasuwayomi {

struct PinchGestureStatus {
    brls::GestureState state;  // START / STAY / END
    float scaleFactor;         // Cumulative scale relative to start (1.0 = no change)
    brls::Point center;        // Midpoint between the two fingers (screen coords)
};

class PinchGestureRecognizer : public brls::GestureRecognizer {
public:
    using Callback = std::function<void(PinchGestureStatus, brls::Sound*)>;

    explicit PinchGestureRecognizer(Callback callback);

    brls::GestureState recognitionLoop(
        brls::TouchState touch, brls::MouseState mouse,
        brls::View* view, brls::Sound* soundToPlay) override;

private:
    Callback m_callback;

    bool m_tracking = false;
    float m_initialDistance = 0.0f;

    static constexpr float MIN_PINCH_DISTANCE = 30.0f;  // Minimum finger distance to start
};

} // namespace vitasuwayomi
