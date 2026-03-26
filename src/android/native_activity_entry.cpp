#ifdef __ANDROID__

#include <android/native_activity.h>
#include <dlfcn.h>
#include <jni.h>
#include <cstddef>

extern int main(int argc, char* argv[]);

extern "C" __attribute__((visibility("default")))
void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    (void)activity;
    (void)savedState;
    (void)savedStateSize;

    using SDLAndroidInitFn = void (*)(JNIEnv*, jclass);
    using SDLSetMainReadyFn = void (*)();

    void* sdlHandle = dlopen("libSDL2.so", RTLD_NOW | RTLD_NOLOAD);
    if (!sdlHandle) {
        sdlHandle = dlopen("libSDL2.so", RTLD_NOW);
    }

    if (sdlHandle && activity && activity->vm && activity->clazz) {
        auto sdlAndroidInit = reinterpret_cast<SDLAndroidInitFn>(dlsym(sdlHandle, "SDL_Android_Init"));
        auto sdlSetMainReady = reinterpret_cast<SDLSetMainReadyFn>(dlsym(sdlHandle, "SDL_SetMainReady"));
        if (sdlAndroidInit && sdlSetMainReady) {
            JNIEnv* env = nullptr;
            if (activity->vm->AttachCurrentThread(&env, nullptr) == JNI_OK && env) {
                sdlAndroidInit(env, reinterpret_cast<jclass>(activity->clazz));
                sdlSetMainReady();
            }
        }
    }

    char* argv[] = { (char*)"VitaSuwayomi", nullptr };
    main(1, argv);
}

#endif
