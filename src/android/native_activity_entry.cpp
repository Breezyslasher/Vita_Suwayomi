#ifdef __ANDROID__

#include <android/native_activity.h>
#include <cstddef>

extern int main(int argc, char* argv[]);

extern "C" __attribute__((visibility("default")))
void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    (void)activity;
    (void)savedState;
    (void)savedStateSize;

    char* argv[] = { (char*)"VitaSuwayomi", nullptr };
    main(1, argv);
}

#endif
