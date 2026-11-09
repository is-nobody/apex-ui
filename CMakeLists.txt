# CMakeLists.txt
# Implementation of CMakeLists for Apex UI
# https://github.com/is-nobody/apex-ui
# MIT license

cmake_minimum_required(VERSION 3.13)
project(ApexUI VERSION 26.11 LANGUAGES C CXX)

# enables gnu extensions for posix functions and statement expressions
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# the c++ facade is c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# options
option(APEXUI_BUILD_SHARED "Build Apex UI as a shared library instead of static" OFF)

# platform window backend + libs
set(APEXUI_WINDOW_SOURCES)
set(APEXUI_PLATFORM_LIBS)

if(WIN32)
    list(APPEND APEXUI_WINDOW_SOURCES
        source/platform/windows/ui_window_windows.c
        source/platform/windows/ui_window_windows_event.c
        source/platform/windows/ui_window_windows_clipboard.c
        source/platform/windows/ui_window_windows_filedialog.c)
    list(APPEND APEXUI_PLATFORM_LIBS shlwapi ws2_32 ole32 shell32)
elseif(APPLE)
    list(APPEND APEXUI_WINDOW_SOURCES
        source/platform/macos/ui_window_macos.c
        source/platform/macos/ui_window_macos_event.c
        source/platform/macos/ui_window_macos_clipboard.c
        source/platform/macos/ui_window_macos_filedialog.c)
    list(APPEND APEXUI_PLATFORM_LIBS
        "-framework AppKit"
        "-framework Cocoa"
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework CoreFoundation")
elseif(ANDROID)
    # android backend: native activity glue + software surface
    list(APPEND APEXUI_WINDOW_SOURCES
        source/platform/android/ui_window_android.c
        source/platform/android/ui_window_android_event.c
        source/platform/android/ui_window_android_clipboard.c
        source/platform/android/ui_window_android_filedialog.c
        source/platform/android/ui_window_android_jni.c)
    list(APPEND APEXUI_PLATFORM_LIBS log android)
else()
    list(APPEND APEXUI_WINDOW_SOURCES
        source/platform/linux/ui_window_linux.c
        source/platform/linux/ui_window_linux_event.c
        source/platform/linux/ui_window_linux_clipboard.c
        source/platform/linux/ui_window_linux_filedialog.c)
    find_package(X11 REQUIRED)
    find_package(Fontconfig REQUIRED)
    list(APPEND APEXUI_PLATFORM_LIBS
        m z dl
        X11::X11
        X11::Xext
        Fontconfig::Fontconfig)
endif()

# portable library sources (apex_ui.c / apex_ui.cpp are the public api)
set(APEXUI_SOURCES
    apex_ui.c
    apex_ui.cpp

    source/common/apexui_value.c
    source/common/ui_hash.c
    source/common/ui_arena.c
    source/common/ui_table.c

    source/core/ui_context.c
    source/core/ui_state.c
    source/core/ui_events.c
    source/core/ui_widget.c
    source/core/ui_build.c
    source/core/ui_runtime.c

    source/widgets/ui_input.c
    source/widgets/ui_dialog.c
    source/widgets/ui_notif.c
    source/widgets/ui_menu.c
    source/widgets/ui_menubar.c
    source/widgets/ui_context_menu.c
    source/widgets/ui_table.c
    source/widgets/ui_tree.c
    source/widgets/ui_tabs.c
    source/widgets/ui_number_input.c

    source/interaction/ui_hitgrid.c
    source/interaction/ui_damage.c
    source/interaction/ui_handle.c

    source/layout/ui_layout.c

    source/render/ui_painter.c
    source/render/ui_render.c
    source/render/ui_render_dialog.c
    source/render/ui_render_notif.c
    source/render/ui_titlebar.c
    source/render/ui_text.c
    source/render/ui_font.c
    source/render/ui_font_system.c
)

if(APEXUI_BUILD_SHARED)
    add_library(apexui SHARED ${APEXUI_SOURCES} ${APEXUI_WINDOW_SOURCES})
    set_target_properties(apexui PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
else()
    add_library(apexui STATIC ${APEXUI_SOURCES} ${APEXUI_WINDOW_SOURCES})
endif()

# apex_ui.h is the public header; internal headers stay private to the library
target_include_directories(apexui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${CMAKE_CURRENT_SOURCE_DIR}/source/common
        ${CMAKE_CURRENT_SOURCE_DIR}/source/core
        ${CMAKE_CURRENT_SOURCE_DIR}/source/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/source/interaction
        ${CMAKE_CURRENT_SOURCE_DIR}/source/layout
        ${CMAKE_CURRENT_SOURCE_DIR}/source/render
        ${CMAKE_CURRENT_SOURCE_DIR}/source/platform/linux
        ${CMAKE_CURRENT_SOURCE_DIR}/source/platform/windows
        ${CMAKE_CURRENT_SOURCE_DIR}/source/platform/macos
        ${CMAKE_CURRENT_SOURCE_DIR}/source/platform/android
)

# baseline warnings and posix compatibility
set(BASE_FLAGS -Wall -Wextra -D_DEFAULT_SOURCE)

# applies gcc/clang-specific performance and base flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(apexui PRIVATE
        ${BASE_FLAGS}
        $<$<CONFIG:Release>:-O3>
    )

    include(CheckIPOSupported)
    check_ipo_supported(RESULT APEXUI_IPO_OK OUTPUT APEXUI_IPO_ERR)
    if(APEXUI_IPO_OK)
        set_property(TARGET apexui PROPERTY
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(WARNING "IPO/LTO not supported: ${APEXUI_IPO_ERR}")
    endif()
endif()

# windows-specific defines to suppress deprecation warnings
if(WIN32)
    target_compile_definitions(apexui PRIVATE
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
endif()

# threading support for background i/o workers
find_package(Threads REQUIRED)
target_link_libraries(apexui PUBLIC Threads::Threads)

# platform-specific libraries
target_link_libraries(apexui PUBLIC ${APEXUI_PLATFORM_LIBS})

# strips debug symbols in release builds
if(APEXUI_BUILD_SHARED)
    add_custom_command(TARGET apexui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Stripping $<TARGET_FILE:apexui>"
        COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:apexui>
        COMMENT "Strip $<TARGET_FILE_NAME:apexui>"
        VERBATIM
    )
endif()

# install rules: public headers + the library
include(GNUInstallDirs)
install(TARGETS apexui
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES apex_ui.h apex_ui.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

#[[
# static library
cmake -S . -B build/ -DCMAKE_BUILD_TYPE=Release
cmake --build build/ --parallel

# shared library
cmake -S . -B build/ \
    -DCMAKE_BUILD_TYPE=Release \
    -DAPEXUI_BUILD_SHARED=ON
cmake --build build/ --parallel
]]