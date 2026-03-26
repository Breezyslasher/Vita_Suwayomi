#ifdef __ANDROID__

#include <android/native_activity.h>
#include <pthread.h>
#include <cstddef>

extern int main(int argc, char* argv[]);

static void* runMain(void*) {
    char* argv[] = { (char*)"VitaSuwayomi", nullptr };
    main(1, argv);
    return nullptr;
}

extern "C" __attribute__((visibility("default")))
void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    (void)activity;
    (void)savedState;
    (void)savedStateSize;

    pthread_t thread;
    if (pthread_create(&thread, nullptr, runMain, nullptr) == 0) {
        pthread_detach(thread);
    }
}

#endif
