/**
 * VitaSuwayomi - Pinch Gesture Recognizer implementation
 *
 * Polls platform touch hardware directly for two-finger input:
 *   - Vita: sceTouchPeek() with coords 0-1919 x 0-1087 (halved for 960x544)
 *   - SDL:  SDL_GetTouchFinger() with normalized 0.0-1.0 coords
 */

#include "view/pinch_gesture.hpp"
#include <cmath>

#ifdef __vita__
#include <psp2/touch.h>
#elif defined(__SDL2__)
#include <SDL2/SDL.h>
#endif

namespace vitasuwayomi {

PinchGestureRecognizer::PinchGestureRecognizer(Callback callback)
    : m_callback(std::move(callback))
{
    this->state = brls::GestureState::FAILED;

#ifdef __vita__
    // Ensure front touch sampling is enabled
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
#endif
}

brls::GestureState PinchGestureRecognizer::recognitionLoop(
    brls::TouchState touch, brls::MouseState mouse,
    brls::View* view, brls::Sound* soundToPlay)
{
#ifdef __vita__
    SceTouchData touchData;
    int ret = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touchData, 1);

    if (ret < 0 || touchData.reportNum < 2) {
        // Fewer than 2 fingers — end pinch if we were tracking
        if (m_tracking) {
            m_tracking = false;
            this->state = brls::GestureState::END;

            PinchGestureStatus status{};
            status.state = brls::GestureState::END;
            status.scaleFactor = 1.0f;
            status.center = {0, 0};

            if (m_callback) {
                m_callback(status, soundToPlay);
            }
            return this->state;
        }

        this->state = brls::GestureState::FAILED;
        return this->state;
    }

    // Two fingers detected — compute distance and center
    // Vita touch coords: 0-1919 x 0-1087, screen is 960x544
    float x0 = touchData.report[0].x / 2.0f;
    float y0 = touchData.report[0].y / 2.0f;
    float x1 = touchData.report[1].x / 2.0f;
    float y1 = touchData.report[1].y / 2.0f;

    float dx = x1 - x0;
    float dy = y1 - y0;
    float distance = std::sqrt(dx * dx + dy * dy);

    brls::Point center = {(x0 + x1) / 2.0f, (y0 + y1) / 2.0f};

    if (!m_tracking) {
        // Start pinch
        if (distance < MIN_PINCH_DISTANCE) {
            this->state = brls::GestureState::FAILED;
            return this->state;
        }

        m_tracking = true;
        m_initialDistance = distance;
        this->state = brls::GestureState::START;

        PinchGestureStatus status{};
        status.state = brls::GestureState::START;
        status.scaleFactor = 1.0f;
        status.center = center;

        if (m_callback) {
            m_callback(status, soundToPlay);
        }
    } else {
        // Continue pinch — fire on every frame for live update
        float scale = (m_initialDistance > 0.0f) ? (distance / m_initialDistance) : 1.0f;
        this->state = brls::GestureState::STAY;

        PinchGestureStatus status{};
        status.state = brls::GestureState::STAY;
        status.scaleFactor = scale;
        status.center = center;

        if (m_callback) {
            m_callback(status, soundToPlay);
        }
    }

    return this->state;

#elif defined(__SDL2__)
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool hasTwoFingers = false;

    int devices = SDL_GetNumTouchDevices();
    for (int d = 0; d < devices && !hasTwoFingers; d++) {
        SDL_TouchID device = SDL_GetTouchDevice(d);
        if (SDL_GetNumTouchFingers(device) >= 2) {
            SDL_Finger* f0 = SDL_GetTouchFinger(device, 0);
            SDL_Finger* f1 = SDL_GetTouchFinger(device, 1);
            if (f0 && f1) {
                float w = brls::Application::contentWidth;
                float h = brls::Application::contentHeight;
                x0 = f0->x * w;
                y0 = f0->y * h;
                x1 = f1->x * w;
                y1 = f1->y * h;
                hasTwoFingers = true;
            }
        }
    }

    if (!hasTwoFingers) {
        if (m_tracking) {
            m_tracking = false;
            this->state = brls::GestureState::END;

            PinchGestureStatus status{};
            status.state = brls::GestureState::END;
            status.scaleFactor = 1.0f;
            status.center = {0, 0};

            if (m_callback) {
                m_callback(status, soundToPlay);
            }
            return this->state;
        }

        this->state = brls::GestureState::FAILED;
        return this->state;
    }

    {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float distance = std::sqrt(dx * dx + dy * dy);
        brls::Point center = {(x0 + x1) / 2.0f, (y0 + y1) / 2.0f};

        if (!m_tracking) {
            if (distance < MIN_PINCH_DISTANCE) {
                this->state = brls::GestureState::FAILED;
                return this->state;
            }

            m_tracking = true;
            m_initialDistance = distance;
            this->state = brls::GestureState::START;

            PinchGestureStatus status{};
            status.state = brls::GestureState::START;
            status.scaleFactor = 1.0f;
            status.center = center;

            if (m_callback) {
                m_callback(status, soundToPlay);
            }
        } else {
            float scale = (m_initialDistance > 0.0f) ? (distance / m_initialDistance) : 1.0f;
            this->state = brls::GestureState::STAY;

            PinchGestureStatus status{};
            status.state = brls::GestureState::STAY;
            status.scaleFactor = scale;
            status.center = center;

            if (m_callback) {
                m_callback(status, soundToPlay);
            }
        }

        return this->state;
    }

#else
    this->state = brls::GestureState::FAILED;
    return this->state;
#endif
}

} // namespace vitasuwayomi
