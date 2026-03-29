cmake_minimum_required(VERSION 3.6)

project(jni)

set(APP_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")

if (NOT EXISTS "${APP_SOURCE_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "APP_SOURCE_ROOT does not contain CMakeLists.txt: ${APP_SOURCE_ROOT}")
endif()

# armeabi-v7a requires cpufeatures library
# include(AndroidNdkModules)
# android_ndk_import_module_cpufeatures()

set(MPV_PREFIX "${APP_SOURCE_ROOT}/jniLibs/${ANDROID_ABI}")
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${MPV_PREFIX})
set(ENV{PKG_CONFIG_LIBDIR} ${MPV_PREFIX}/lib/pkgconfig)
set(CMAKE_PREFIX_PATH ${MPV_PREFIX})
list(PREPEND CMAKE_FIND_ROOT_PATH ${MPV_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

link_directories(${MPV_PREFIX}/lib)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -L${MPV_PREFIX}/lib")

set(APP_PLATFORM_LIB
    ${MPV_PREFIX}/lib/libmpv.so
    ${MPV_PREFIX}/lib/libavcodec.so
    ${MPV_PREFIX}/lib/libavdevice.so
    ${MPV_PREFIX}/lib/libavfilter.so
    ${MPV_PREFIX}/lib/libavformat.so
    ${MPV_PREFIX}/lib/libswresample.so
    ${MPV_PREFIX}/lib/libswscale.so
    ${MPV_PREFIX}/lib/libavutil.so
)

# FIX: Do NOT use libromfs for Android.
#
# Previously USE_LIBROMFS=ON was set here, which embedded resources into the .so
# via a romfs blob. This caused the romfs bundle to be missing borealis system
# resources (i18n/hints.json, img/sys/), causing brls::fatal() in createFromXMLFile.
#
# Switchfin (the upstream this app is based on) does NOT use libromfs for Android.
# Instead it uses RESOURCE_PREFIX="resources/" and SDL2's AAssetManager bridge:
# SDL_RWFromFile("resources/foo") on Android reads from the APK's assets/ folder.
# Resources are packaged into assets/ by Gradle (see build.gradle assets.srcDirs).
# This approach is simpler and matches the upstream exactly.
#
# We keep libromfs disabled on Android and load resources from APK assets
# via SDL2/AAssetManager instead.
set(USE_LIBROMFS OFF CACHE BOOL "" FORCE)

# Borealis currently checks for libromfs-generator on Android at configure time,
# even when USE_LIBROMFS=OFF. Point it to the real generator when present, or to
# a tiny placeholder script so configure can proceed when libromfs is unused.
set(_LIBROMFS_GENERATOR "${APP_SOURCE_ROOT}/lib/borealis/libromfs-generator")
if (EXISTS "${_LIBROMFS_GENERATOR}")
    set(LIBROMFS_PREBUILT_GENERATOR "${_LIBROMFS_GENERATOR}" CACHE STRING "" FORCE)
else()
    set(_LIBROMFS_GENERATOR_STUB "${CMAKE_BINARY_DIR}/libromfs-generator-stub.sh")
    file(WRITE "${_LIBROMFS_GENERATOR_STUB}" "#!/usr/bin/env sh\necho 'libromfs-generator stub: USE_LIBROMFS is OFF for Android.' >&2\nexit 1\n")
    execute_process(COMMAND chmod +x "${_LIBROMFS_GENERATOR_STUB}")
    set(LIBROMFS_PREBUILT_GENERATOR "${_LIBROMFS_GENERATOR_STUB}" CACHE STRING "" FORCE)
endif()
unset(_LIBROMFS_GENERATOR)
unset(_LIBROMFS_GENERATOR_STUB)

# app option
set(PLATFORM_ANDROID ON CACHE BOOL "")
set(USE_SYSTEM_CURL ON CACHE BOOL "")
set(USE_SYSTEM_SDL2 ON CACHE BOOL "")
set(USE_GLES3 ON CACHE BOOL "")
set(SIMPLE_HIGHLIGHT ON CACHE BOOL "")
# USE_LIBROMFS is forced OFF above, giving RESOURCE_PREFIX="resources/"
# which SDL2 resolves via AAssetManager from the APK assets/ folder.

# https://github.com/curl/curl/issues/12086
# if (ANDROID_ABI STREQUAL armeabi-v7a)
#     set(HAVE_FSEEKO 0)
# endif()
add_subdirectory("${APP_SOURCE_ROOT}" "${CMAKE_BINARY_DIR}/vitasuwayomi")
