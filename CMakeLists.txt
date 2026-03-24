cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Platform selection
# Pass -DPLATFORM_PSV=ON, -DPLATFORM_SWITCH=ON, or -DPLATFORM_DESKTOP=ON
# to cmake when configuring. Exactly one must be ON.
# Example: cmake -B build -DPLATFORM_DESKTOP=ON
# ---------------------------------------------------------------------------

option(PLATFORM_PSV     "Build for PlayStation Vita"                   OFF)
option(PLATFORM_SWITCH  "Build for Nintendo Switch"                    OFF)
option(PLATFORM_DESKTOP "Build for Desktop (Linux/macOS/Windows)"      OFF)
option(PLATFORM_ANDROID "Build for Android"                            OFF)

# Default to desktop when nothing is specified
set(_PLATFORM_COUNT 0)
foreach(_P PSV SWITCH DESKTOP ANDROID)
    if(PLATFORM_${_P})
        math(EXPR _PLATFORM_COUNT "${_PLATFORM_COUNT} + 1")
    endif()
endforeach()
if(_PLATFORM_COUNT EQUAL 0)
    message(STATUS "No platform selected — defaulting to PLATFORM_DESKTOP=ON")
    set(PLATFORM_DESKTOP ON CACHE BOOL "Build for Desktop" FORCE)
elseif(_PLATFORM_COUNT GREATER 1)
    message(FATAL_ERROR "More than one PLATFORM_* flag is ON. Set exactly one.")
endif()

# ---------------------------------------------------------------------------
# Borealis paths — must be set before the cmake includes below
# ---------------------------------------------------------------------------
set(BOREALIS_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

# Forward into borealis-internal cache variables
set(PLATFORM_DESKTOP ${PLATFORM_DESKTOP} CACHE BOOL "" FORCE)
set(PLATFORM_SWITCH  ${PLATFORM_SWITCH}  CACHE BOOL "" FORCE)
set(PLATFORM_PSV     ${PLATFORM_PSV}     CACHE BOOL "" FORCE)
set(PLATFORM_PS4     OFF                 CACHE BOOL "" FORCE)
set(PLATFORM_IOS     OFF                 CACHE BOOL "" FORCE)
set(PLATFORM_ANDROID ${PLATFORM_ANDROID} CACHE BOOL "" FORCE)

if(PLATFORM_PSV)
    set(BOREALIS_USE_GXM ON  CACHE BOOL "Use GXM renderer for PS Vita" FORCE)
    set(USE_VITA_SHARK  OFF  CACHE BOOL "Use VitaShark for shader compilation" FORCE)
endif()

set(USE_LIBROMFS OFF CACHE BOOL "Use libromfs" FORCE)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# ---------------------------------------------------------------------------
# Project (after toolchain so cross-compilers are in effect)
# ---------------------------------------------------------------------------
project(VitaSuwayomi VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -frtti -fexceptions")

# ---------------------------------------------------------------------------
# Vita VPK metadata
# ---------------------------------------------------------------------------
if(PLATFORM_PSV)
    set(VITA_APP_NAME "VitaSuwayomi")
    set(VITA_TITLEID  "VSWY00001")
    set(VITA_VERSION  "01.00")
    set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1 -d ATTRIBUTE2=12")
endif()

# ---------------------------------------------------------------------------
# PSV borealis patch
# ---------------------------------------------------------------------------
if(PLATFORM_PSV)
    set(PSV_PLATFORM_PATCH  ${CMAKE_CURRENT_SOURCE_DIR}/patches/psv_platform.cpp)
    set(PSV_PLATFORM_TARGET ${BOREALIS_LIBRARY}/lib/platforms/psv/psv_platform.cpp)
    if(EXISTS ${PSV_PLATFORM_PATCH})
        message(STATUS "Applying patch: psv_platform.cpp")
        configure_file(${PSV_PLATFORM_PATCH} ${PSV_PLATFORM_TARGET} COPYONLY)
    else()
        message(FATAL_ERROR "Patch file not found: ${PSV_PLATFORM_PATCH}")
    endif()
endif()

add_subdirectory(${BOREALIS_LIBRARY})

# ---------------------------------------------------------------------------
# Application sources
# ---------------------------------------------------------------------------
set(APP_SOURCES
    src/main.cpp
    src/app/application.cpp
    src/app/suwayomi_client.cpp
    src/app/downloads_manager.cpp
    src/activity/main_activity.cpp
    src/activity/login_activity.cpp
    src/activity/reader_activity.cpp
    src/view/library_section_tab.cpp
    src/view/history_tab.cpp
    src/view/search_tab.cpp
    src/view/settings_tab.cpp
    src/view/media_detail_view.cpp
    src/view/media_item_cell.cpp
    src/view/recycling_grid.cpp
    src/view/rotatable_box.cpp
    src/view/rotatable_image.cpp
    src/view/rotatable_label.cpp
    src/view/webtoon_scroll_view.cpp
    src/view/downloads_tab.cpp
    src/view/extensions_tab.cpp
    src/view/source_browse_tab.cpp
    src/view/horizontal_scroll_row.cpp
    src/view/tracking_search_view.cpp
    src/view/migrate_search_view.cpp
    src/view/storage_view.cpp
    src/view/long_press_gesture.cpp
    src/view/pinch_gesture.cpp
    src/utils/http_client.cpp
    src/utils/image_loader.cpp
    src/utils/library_cache.cpp
    src/utils/perf_overlay.cpp
)

# vita_stubs.c only needed on Vita (no-op stdio locks, SDL_OpenURL stub)
if(PLATFORM_PSV)
    list(APPEND APP_SOURCES src/utils/vita_stubs.c)
endif()

set(APP_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${BOREALIS_LIBRARY}/include
    ${BOREALIS_LIBRARY}/include/borealis/extern
)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
if(PLATFORM_ANDROID)
    add_library(${PROJECT_NAME} SHARED ${APP_SOURCES})
else()
    add_executable(${PROJECT_NAME} ${APP_SOURCES})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${APP_INCLUDES})

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
target_link_libraries(${PROJECT_NAME}
    borealis
    ${APP_PLATFORM_LIB}
    curl
    mbedtls
    mbedx509
    mbedcrypto
    -Wl,--start-group
    mpv
    avfilter
    avformat
    avcodec
    swresample
    swscale
    avutil
    -Wl,--end-group
    zstd
    ass
    freetype
    harfbuzz
    fribidi
    png
    bz2
    webp
    z
    c
)

if(NOT PLATFORM_SWITCH)
    target_link_libraries(${PROJECT_NAME} mp3lame)
endif()

if(PLATFORM_PSV)
    target_link_libraries(${PROJECT_NAME}
        vita2d
        SceNet_stub
        SceNetCtl_stub
        SceHttp_stub
        SceSsl_stub
        ScePgf_stub
        SceRtc_stub
        SceShellSvc_stub
        SceAppMgr_stub
        SceTouch_stub
        SceAudiodec_stub
        SceVideodec_stub
        SceAvcodec_stub
        SceCodecEngine_stub
        SceAudio_stub
        SceVshBridge_stub
    )
elseif(PLATFORM_SWITCH)
    target_link_libraries(${PROJECT_NAME} nx)
elseif(PLATFORM_DESKTOP)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# Platform packaging / post-build
# ---------------------------------------------------------------------------
if(PLATFORM_PSV)
    vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE NOASLR)

    vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
        VERSION ${VITA_VERSION}
        NAME    ${VITA_APP_NAME}

        FILE sce_sys/icon0.png                      sce_sys/icon0.png
        FILE sce_sys/pic0.png                       sce_sys/pic0.png
        FILE sce_sys/livearea/contents/bg.png       sce_sys/livearea/contents/bg.png
        FILE sce_sys/livearea/contents/startup.png  sce_sys/livearea/contents/startup.png
        FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml

        FILE ${BOREALIS_DIR}/resources/i18n/en-US/hints.json resources/i18n/en-US/hints.json
        FILE resources/i18n/en-US/main.json                  resources/i18n/en-US/main.json

        FILE ${BOREALIS_DIR}/resources/img/sys/battery_back_dark.png  resources/img/sys/battery_back_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/battery_back_light.png resources/img/sys/battery_back_light.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_0_dark.png        resources/img/sys/wifi_0_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_0_light.png       resources/img/sys/wifi_0_light.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_1_dark.png        resources/img/sys/wifi_1_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_1_light.png       resources/img/sys/wifi_1_light.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_2_dark.png        resources/img/sys/wifi_2_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_2_light.png       resources/img/sys/wifi_2_light.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_3_dark.png        resources/img/sys/wifi_3_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_3_light.png       resources/img/sys/wifi_3_light.png
        FILE ${BOREALIS_DIR}/resources/img/sys/ethernet_dark.png      resources/img/sys/ethernet_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/ethernet_light.png     resources/img/sys/ethernet_light.png

        FILE resources/xml/activity/main.xml     resources/xml/activity/main.xml
        FILE resources/xml/activity/login.xml    resources/xml/activity/login.xml
        FILE resources/xml/activity/reader.xml   resources/xml/activity/reader.xml
        FILE resources/xml/tabs/library.xml      resources/xml/tabs/library.xml
        FILE resources/xml/tabs/search.xml       resources/xml/tabs/search.xml
        FILE resources/xml/tabs/settings.xml     resources/xml/tabs/settings.xml
        FILE resources/xml/tabs/extensions.xml   resources/xml/tabs/extensions.xml
        FILE resources/xml/view/manga_detail.xml resources/xml/view/manga_detail.xml
        FILE resources/xml/view/manga_item.xml   resources/xml/view/manga_item.xml
        FILE resources/xml/view/chapter_item.xml resources/xml/view/chapter_item.xml

        FILE resources/icons/a.png                        resources/icons/a.png
        FILE resources/icons/az.png                       resources/icons/az.png
        FILE resources/icons/za.png                       resources/icons/za.png
        FILE resources/icons/back.png                     resources/icons/back.png
        FILE resources/icons/checkbox.png                 resources/icons/checkbox.png
        FILE resources/icons/checkbox_checked.png         resources/icons/checkbox_checked.png
        FILE resources/icons/checkbox_crossed.png         resources/icons/checkbox_crossed.png
        FILE resources/icons/circle.png                   resources/icons/circle.png
        FILE resources/icons/cross.png                    resources/icons/cross.png
        FILE resources/icons/download.png                 resources/icons/download.png
        FILE resources/icons/hide.png                     resources/icons/hide.png
        FILE resources/icons/history.png                  resources/icons/history.png
        FILE resources/icons/hot.png                      resources/icons/hot.png
        FILE resources/icons/import.png                   resources/icons/import.png
        FILE resources/icons/left.png                     resources/icons/left.png
        FILE resources/icons/menu.png                     resources/icons/menu.png
        FILE resources/icons/mini_star.png                resources/icons/mini_star.png
        FILE resources/icons/options.png                  resources/icons/options.png
        FILE resources/icons/radio.png                    resources/icons/radio.png
        FILE resources/icons/radio_checked.png            resources/icons/radio_checked.png
        FILE resources/icons/refresh.png                  resources/icons/refresh.png
        FILE resources/icons/right.png                    resources/icons/right.png
        FILE resources/icons/search.png                   resources/icons/search.png
        FILE resources/icons/show.png                     resources/icons/show.png
        FILE resources/icons/sort-1-9.png                 resources/icons/sort-1-9.png
        FILE resources/icons/sort-9-1.png                 resources/icons/sort-9-1.png
        FILE resources/icons/sort-clock-ascending.png     resources/icons/sort-clock-ascending.png
        FILE resources/icons/sort-clock-descending.png    resources/icons/sort-clock-descending.png
        FILE resources/icons/sort-calendar-ascending.png  resources/icons/sort-calendar-ascending.png
        FILE resources/icons/sort-calendar-descending.png resources/icons/sort-calendar-descending.png
        FILE resources/icons/book-open-page-variant.png   resources/icons/book-open-page-variant.png
        FILE resources/icons/book-multiple.png            resources/icons/book-multiple.png
        FILE resources/icons/book-arrow-down.png          resources/icons/book-arrow-down.png
        FILE resources/icons/star.png                     resources/icons/star.png
        FILE resources/icons/tag.png                      resources/icons/tag.png
        FILE resources/icons/web.png                      resources/icons/web.png
        FILE resources/icons/filter-menu-outline.png      resources/icons/filter-menu-outline.png
        FILE resources/icons/format-list-group.png        resources/icons/format-list-group.png

        FILE resources/images/circle_button.png   resources/images/circle_button.png
        FILE resources/images/cross_button.png    resources/images/cross_button.png
        FILE resources/images/dpad.png            resources/images/dpad.png
        FILE resources/images/gradient.png        resources/images/gradient.png
        FILE resources/images/l_button.png        resources/images/l_button.png
        FILE resources/images/logo-small.png      resources/images/logo-small.png
        FILE resources/images/logo.png            resources/images/logo.png
        FILE resources/images/r_button.png        resources/images/r_button.png
        FILE resources/images/select_button.png   resources/images/select_button.png
        FILE resources/images/square_button.png   resources/images/square_button.png
        FILE resources/images/start_button.png    resources/images/start_button.png
        FILE resources/images/triangle_button.png resources/images/triangle_button.png

        FILE resources/font/font.ttf                      resources/font/font.ttf
        FILE resources/font/icon.ttf                      resources/font/icon.ttf
        FILE resources/font/switch_font.ttf               resources/font/switch_font.ttf
        FILE resources/font/switch_icons.ttf              resources/font/switch_icons.ttf
        FILE resources/material/MaterialIcons-Regular.ttf resources/material/MaterialIcons-Regular.ttf
    )

elseif(PLATFORM_SWITCH)
    nx_create_nro(${PROJECT_NAME}
        NAME   "VitaSuwayomi"
        AUTHOR "Breezyslasher"
        ICON   ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png
    )

elseif(PLATFORM_DESKTOP)
    # Copy resources directory next to the binary so relative paths resolve
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/resources
        COMMENT "Copying resources for desktop build"
    )
endif()
