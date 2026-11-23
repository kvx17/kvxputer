#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.31)

project(kvxputerzero VERSION 0.1.0 LANGUAGES C CXX)

# libcamera 0.8 (Cardputer / App Center) uses std::span in public headers.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# CMake configure options

# USE_DESKTOP: whether to build desktop emulator, useful when you want preview with LVGL simulator
# For instance, '-DUSE_DESKTOP=OFF' suggest building for CM0
set(USE_DESKTOP ON CACHE BOOL "Build the desktop emulator target with LVGL SDL support")
set(APP_NAME "${PROJECT_NAME}" CACHE STRING "Application name used for installed asset lookup")
set(APP_ASSETS_ROOT "" CACHE PATH "Optional runtime assets root. Expected subdirectories include fonts/")
set(APP_CONFIG_FILE "" CACHE FILEPATH "Application config file; defaults to the repository file on desktop and /etc/kvxputerzero.conf on device")
set(APP_KEY_INPUT_DEVICE "" CACHE STRING "Optional Linux evdev keyboard/keypad device path, e.g. /dev/input/event0")
set(APP_FRAMEBUFFER_DEVICE "/dev/fb0" CACHE STRING "Linux framebuffer device path used by embedded builds")
set(APP_DRM_DEVICE "/dev/dri/card0" CACHE STRING "Linux DRM device path used when APP_USE_DRM=ON")
set(APP_DRM_CONNECTOR_ID "-1" CACHE STRING "DRM connector id used when APP_USE_DRM=ON; -1 auto-selects")
set(APP_USE_DRM OFF CACHE BOOL "Enable LVGL Linux DRM/KMS backend for embedded Linux builds")
set(CM0_ALLOW_FETCH_DEPS ON CACHE BOOL "Allow downloading missing CM0 BSP/dependencies during cross builds")
# Export compile commands
# Used for IDE IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

if(NOT APP_CONFIG_FILE)
    if(USE_DESKTOP)
        set(APP_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/kvxputerzero.conf")
    else()
        set(APP_CONFIG_FILE "/etc/kvxputerzero.conf")
    endif()
endif()


include(FetchContent)


# lvgl - main ui framework
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS    OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/src/config/lv_conf.h" CACHE PATH "" FORCE)

# Manually download LVGL dependencies
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.5.0
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(lvgl)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
endif()
add_subdirectory("${lvgl_SOURCE_DIR}" "${lvgl_BINARY_DIR}" EXCLUDE_FROM_ALL)
set(LVGL_SOURCE_DIR "${lvgl_SOURCE_DIR}")
get_filename_component(LVGL_INCLUDE_PARENT "${LVGL_SOURCE_DIR}" DIRECTORY)

# other misc dependencies
# desktop builds
if(USE_DESKTOP)
    message(STATUS "Configuring DESKTOP build")

    # =========================
    # PNG
    # =========================
    find_package(PNG REQUIRED)

    # =========================
    # JPEG (platform abstraction handled by package manager)
    # =========================
    find_package(JPEG REQUIRED)

    if(TARGET libjpeg-turbo::libjpeg-turbo)
        set(JPEG_TARGET libjpeg-turbo::libjpeg-turbo)
    elseif(TARGET JPEG::JPEG)
        set(JPEG_TARGET JPEG::JPEG)
    else()
        message(FATAL_ERROR "No JPEG provider found")
    endif()

    # =========================
    # Freetype
    # =========================

    find_package(Freetype REQUIRED)
    # =========================
    # SDL2
    # =========================
    find_package(SDL2 CONFIG REQUIRED)

    # =========================
    # fmt
    # =========================
    find_package(fmt CONFIG REQUIRED)

    # =========================
    # ZLIB
    # =========================
    find_package(ZLIB REQUIRED)

    # =========================
    # Link safely
    # =========================
    set(LVGL_DESKTOP_DEPS
        ZLIB::ZLIB
        PNG::PNG
        ${JPEG_TARGET}
        Freetype::Freetype
        SDL2::SDL2
    )

    foreach(dep IN LISTS LVGL_DESKTOP_DEPS)
        if(TARGET ${dep})
            target_link_libraries(lvgl PUBLIC ${dep})
        endif()
    endforeach()

    if(SDL2_FOUND AND NOT TARGET SDL2::SDL2)
        target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
    endif()

    set(KVX_HAVE_LIBCAMERA 0)
    target_compile_definitions(lvgl PUBLIC USE_DESKTOP=1 KVX_HAVE_LIBCAMERA=0)
    
    if(MSVC)
        target_compile_options(lvgl PUBLIC /utf-8)
    endif()

# Cross builds
# This search individual dependencies from the BSP folder
# and add to the target
# this may not be the good way...
else()
    message(STATUS "Configuring CM0/aarch64 Linux build")

    if(NOT CM0_SDK_ROOT OR NOT EXISTS "${CM0_SDK_ROOT}")
        message(FATAL_ERROR
            "CM0 SDK sysroot not found or invalid.\n"
            "The cp0 toolchain should download the BSP before project() runs, "
            "or configure with -DCM0_SDK_ROOT=<path_to_sdk_sysroot>."
        )
    endif()

    if(CMAKE_SYSROOT)
        set(CM0_SYSROOT "${CMAKE_SYSROOT}")
    else()
        set(CM0_SYSROOT "${CM0_SDK_ROOT}")
    endif()
    if(NOT CM0_MULTIARCH)
        set(CM0_MULTIARCH "aarch64-linux-gnu")
    endif()
    message(STATUS "Using CM0 SDK root: ${CM0_SDK_ROOT}")
    message(STATUS "Using CM0 dependency root: ${CM0_SYSROOT}")

    set(CM0_INCLUDE_HINTS
        "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/include"
        "${CM0_SYSROOT}/usr/local/include"
        "${CM0_SYSROOT}/include/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/include"
    )
    set(CM0_LIBRARY_HINTS
        "${CM0_SYSROOT}/usr/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/lib"
        "${CM0_SYSROOT}/usr/local/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/local/lib"
        "${CM0_SYSROOT}/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/lib"
    )

    function(cm0_find_header out_var header_name)
        find_path(${out_var}
            NAMES ${header_name}
            HINTS ${ARGN}
            NO_DEFAULT_PATH
        )
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    function(cm0_find_library out_var)
        find_library(${out_var}
            NAMES ${ARGN}
            HINTS ${CM0_LIBRARY_HINTS}
            NO_DEFAULT_PATH
        )
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    function(cm0_add_imported_library target_name library_path include_dirs)
        if(NOT TARGET ${target_name})
            add_library(${target_name} UNKNOWN IMPORTED)
            set_target_properties(${target_name} PROPERTIES
                IMPORTED_LOCATION "${library_path}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_dirs}"
            )
        endif()
    endfunction()

    # Display backend, DRM is optional. All lookups stay inside the BSP sysroot.
    if(APP_USE_DRM)
        cm0_find_header(DRM_XF86_INCLUDE_DIR xf86drm.h
            "${CM0_SYSROOT}/usr/include"
            "${CM0_SYSROOT}/usr/include/libdrm"
        )
        cm0_find_header(DRM_XF86_MODE_INCLUDE_DIR xf86drmMode.h
            "${CM0_SYSROOT}/usr/include"
            "${CM0_SYSROOT}/usr/include/libdrm"
        )
        cm0_find_header(DRM_MODE_INCLUDE_DIR drm.h
            "${CM0_SYSROOT}/usr/include/drm"
            "${CM0_SYSROOT}/usr/include/libdrm"
            "${CM0_SYSROOT}/usr/include"
        )
        cm0_find_library(DRM_LIBRARY drm drm.so.2)

        if(NOT DRM_XF86_INCLUDE_DIR
           OR NOT DRM_XF86_MODE_INCLUDE_DIR
           OR NOT DRM_MODE_INCLUDE_DIR
           OR NOT DRM_LIBRARY)
            message(FATAL_ERROR
                "APP_USE_DRM=ON but libdrm headers/library were not found in BSP sysroot.\n"
                "Provide them in ${CM0_SYSROOT} or disable with -DAPP_USE_DRM=OFF."
            )
        endif()
        cm0_add_imported_library(CM0::DRM "${DRM_LIBRARY}"
            "${DRM_XF86_INCLUDE_DIR};${DRM_XF86_MODE_INCLUDE_DIR};${DRM_MODE_INCLUDE_DIR}"
        )
        message(STATUS "CM0 DRM display support enabled")
    endif()

    cm0_find_header(FREETYPE_INCLUDE_DIR ft2build.h
        "${CM0_SYSROOT}/usr/include/freetype2"
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(FREETYPE_LIBRARY freetype)
    if(NOT FREETYPE_INCLUDE_DIR OR NOT FREETYPE_LIBRARY)
        message(FATAL_ERROR "Missing Freetype header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::Freetype "${FREETYPE_LIBRARY}" "${FREETYPE_INCLUDE_DIR}")

    cm0_find_header(PNG_INCLUDE_DIR png.h
        "${CM0_SYSROOT}/usr/include/libpng16"
        "${CM0_SYSROOT}/usr/include/libpng"
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(PNG_LIBRARY png16 png)
    if(NOT PNG_INCLUDE_DIR OR NOT PNG_LIBRARY)
        message(FATAL_ERROR "Missing PNG header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::PNG "${PNG_LIBRARY}" "${PNG_INCLUDE_DIR}")

    cm0_find_header(JPEG_INCLUDE_DIR jpeglib.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(JPEG_LIBRARY jpeg)
    if(NOT JPEG_INCLUDE_DIR OR NOT JPEG_LIBRARY)
        message(FATAL_ERROR "Missing JPEG header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::JPEG "${JPEG_LIBRARY}" "${JPEG_INCLUDE_DIR}")

    # CSI camera via libcamera — same as FXCam: link libcamera into the app.
    # App Center users only need packages already on Cardputer (libcamera0.x +
    # libcamera-ipa). The optional device-libcamera cache is for developers so
    # the ELF NEEDED soname matches the Zero OS (currently 0.8).
    set(KVX_HAVE_LIBCAMERA 0)
    set(KVX_DEVICE_LIBCAMERA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.cache/device-libcamera/root")
    set(KVX_DEVICE_LIBCAMERA_LIBDIR
        "${KVX_DEVICE_LIBCAMERA_ROOT}/usr/lib/aarch64-linux-gnu")
    unset(LIBCAMERA_INCLUDE_DIR CACHE)
    unset(LIBCAMERA_LIBRARY CACHE)
    unset(LIBCAMERA_BASE_LIBRARY CACHE)
    if(EXISTS "${KVX_DEVICE_LIBCAMERA_LIBDIR}/libcamera.so.0.8")
        # Pin to device 0.8 so the packaged ELF NEEDs libcamera.so.0.8 (App Center /
        # stock Cardputer). Do not let the cross sysroot redirect to BSP 0.7.
        # Debian -dev layout: include/libcamera/libcamera/camera.h
        if(EXISTS "${KVX_DEVICE_LIBCAMERA_ROOT}/usr/include/libcamera/libcamera/camera.h")
            set(LIBCAMERA_INCLUDE_DIR "${KVX_DEVICE_LIBCAMERA_ROOT}/usr/include/libcamera")
        elseif(EXISTS "${KVX_DEVICE_LIBCAMERA_ROOT}/usr/include/libcamera/camera.h")
            set(LIBCAMERA_INCLUDE_DIR "${KVX_DEVICE_LIBCAMERA_ROOT}/usr/include")
        else()
            set(LIBCAMERA_INCLUDE_DIR "${CM0_SYSROOT}/usr/include")
        endif()
        set(LIBCAMERA_LIBRARY "${KVX_DEVICE_LIBCAMERA_LIBDIR}/libcamera.so.0.8")
        set(LIBCAMERA_BASE_LIBRARY "${KVX_DEVICE_LIBCAMERA_LIBDIR}/libcamera-base.so.0.8")
        message(STATUS "Using device-pulled libcamera 0.8 from ${KVX_DEVICE_LIBCAMERA_ROOT}")
    else()
        set(LIBCAMERA_SEARCH_INC "${CM0_SYSROOT}/usr/include")
        set(LIBCAMERA_SEARCH_LIB ${CM0_LIBRARY_HINTS})
        message(STATUS "Device libcamera 0.8 cache missing — using BSP (run scripts/pull_device_libcamera.sh)")
        find_path(LIBCAMERA_INCLUDE_DIR
            NAMES libcamera/camera.h
            HINTS ${LIBCAMERA_SEARCH_INC}
            NO_DEFAULT_PATH
        )
        find_library(LIBCAMERA_LIBRARY
            NAMES camera libcamera.so
            HINTS ${LIBCAMERA_SEARCH_LIB}
            NO_DEFAULT_PATH
        )
        find_library(LIBCAMERA_BASE_LIBRARY
            NAMES camera-base libcamera-base.so
            HINTS ${LIBCAMERA_SEARCH_LIB}
            NO_DEFAULT_PATH
        )
    endif()
    if(LIBCAMERA_INCLUDE_DIR AND LIBCAMERA_LIBRARY AND LIBCAMERA_BASE_LIBRARY
       AND EXISTS "${LIBCAMERA_LIBRARY}" AND EXISTS "${LIBCAMERA_BASE_LIBRARY}")
        set(KVX_HAVE_LIBCAMERA 1)
        cm0_add_imported_library(CM0::libcamera "${LIBCAMERA_LIBRARY}" "${LIBCAMERA_INCLUDE_DIR}")
        cm0_add_imported_library(CM0::libcamera_base "${LIBCAMERA_BASE_LIBRARY}" "${LIBCAMERA_INCLUDE_DIR}")
        message(STATUS "libcamera enabled: ${LIBCAMERA_LIBRARY}")
    else()
        message(STATUS "libcamera not found — CSI camera backend disabled")
    endif()

    cm0_find_header(ZLIB_INCLUDE_DIR zlib.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(ZLIB_LIBRARY z)
    if(NOT ZLIB_INCLUDE_DIR OR NOT ZLIB_LIBRARY)
        message(FATAL_ERROR "Missing ZLIB header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(ZLIB::ZLIB "${ZLIB_LIBRARY}" "${ZLIB_INCLUDE_DIR}")

    set(CM0_MULTIARCH_INCLUDE_DIRS)
    foreach(CM0_INCLUDE_DIR IN ITEMS
            "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}"
            "${CM0_SYSROOT}/include/${CM0_MULTIARCH}")
        if(EXISTS "${CM0_INCLUDE_DIR}/bits/timesize.h" OR EXISTS "${CM0_INCLUDE_DIR}/bits/wordsize.h")
            list(APPEND CM0_MULTIARCH_INCLUDE_DIRS "${CM0_INCLUDE_DIR}")
            message(STATUS "Found multiarch include: ${CM0_INCLUDE_DIR}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES CM0_MULTIARCH_INCLUDE_DIRS)

    cm0_find_header(FMT_INCLUDE_DIR fmt/format.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(FMT_LIBRARY fmt libfmt.so.10 libfmt.so.10.1.0)
    if(FMT_INCLUDE_DIR AND FMT_LIBRARY)
        cm0_add_imported_library(fmt::fmt "${FMT_LIBRARY}" "${FMT_INCLUDE_DIR}")
    elseif(CM0_ALLOW_FETCH_DEPS)
        message(STATUS "Missing fmt in BSP sysroot, fetching from GitHub because CM0_ALLOW_FETCH_DEPS=ON")
        FetchContent_Declare(
            fmt
            GIT_REPOSITORY https://github.com/fmtlib/fmt.git
            GIT_TAG        10.1.1
            GIT_SHALLOW    TRUE
        )
        set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install rules for app packages" FORCE)
        set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
        set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)
        FetchContent_MakeAvailable(fmt)
        if(TARGET fmt)
            target_include_directories(fmt SYSTEM BEFORE PRIVATE ${CM0_MULTIARCH_INCLUDE_DIRS})
        endif()
        if(TARGET fmt AND NOT TARGET fmt::fmt)
            add_library(fmt::fmt ALIAS fmt)
        endif()
    else()
        message(FATAL_ERROR
            "Missing fmt header/library in BSP sysroot: ${CM0_SYSROOT}\n"
            "Install/copy fmt into the BSP, or reconfigure with -DCM0_ALLOW_FETCH_DEPS=ON."
        )
    endif()

    set_target_properties(CM0::PNG PROPERTIES INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
    set_target_properties(CM0::Freetype PROPERTIES INTERFACE_LINK_LIBRARIES "CM0::PNG;ZLIB::ZLIB")

    cm0_find_library(PTHREAD_LIBRARY pthread libpthread.so.0 libpthread.a)
    cm0_find_library(M_LIBRARY m libm.so libm.so.6 libm.a)
    if(NOT PTHREAD_LIBRARY)
        message(FATAL_ERROR "Missing pthread library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    if(NOT M_LIBRARY)
        message(FATAL_ERROR "Missing math library in BSP sysroot: ${CM0_SYSROOT}")
    endif()

    set(PLATFORM_INCLUDE_DIR ${CM0_MULTIARCH_INCLUDE_DIRS})
    set(PLATFORM_LIBS ${PTHREAD_LIBRARY} ${M_LIBRARY})

    set(LVGL_INCLUDE_DIR
        ${FREETYPE_INCLUDE_DIR}
        ${PNG_INCLUDE_DIR}
        ${JPEG_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIR}
    )
    set(LVGL_LIBS
        CM0::Freetype
        CM0::PNG
        CM0::JPEG
        ZLIB::ZLIB
    )
    if(APP_USE_DRM)
        list(APPEND LVGL_INCLUDE_DIR
            ${DRM_XF86_INCLUDE_DIR}
            ${DRM_XF86_MODE_INCLUDE_DIR}
            ${DRM_MODE_INCLUDE_DIR}
        )
        list(APPEND LVGL_LIBS CM0::DRM)
    endif()

    list(REMOVE_DUPLICATES PLATFORM_INCLUDE_DIR)
    list(REMOVE_DUPLICATES PLATFORM_LIBS)
    list(REMOVE_DUPLICATES LVGL_INCLUDE_DIR)
    list(REMOVE_DUPLICATES LVGL_LIBS)

    target_compile_definitions(lvgl
        PUBLIC
            USE_DESKTOP=0
            APP_USE_DRM=$<BOOL:${APP_USE_DRM}>
            KVX_HAVE_LIBCAMERA=${KVX_HAVE_LIBCAMERA}
    )

    # fix: issue of compiling lvgl on gcc >= 15.0
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # GCC version is available in CMAKE_C_COMPILER_VERSION
        if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0)
            target_compile_options(lvgl PRIVATE -Wno-stringop-overflow)
        endif()
    endif()

    target_include_directories(lvgl
        PUBLIC
            ${LVGL_INCLUDE_DIR}
            ${PLATFORM_INCLUDE_DIR}
    )

    target_link_libraries(lvgl
        PUBLIC
            ${LVGL_LIBS}
            ${PLATFORM_LIBS}
    )

endif()



# ====================================================
# Platform sources
# ====================================================
# Create application platform sources and static library.
# LVGL-facing input/screenshot live in core so CLI can link platform without LVGL.
file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp"
)
list(FILTER PLATFORM_SOURCES EXCLUDE REGEX ".*/linux_input\\.cpp$")
list(FILTER PLATFORM_SOURCES EXCLUDE REGEX ".*/screenshot_service\\.cpp$")

add_library(${PROJECT_NAME}_platform 
    STATIC
    ${PLATFORM_SOURCES}
)

target_include_directories(${PROJECT_NAME}_platform
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tools"
)

target_compile_definitions(${PROJECT_NAME}_platform
    PUBLIC
        APP_FRAMEBUFFER_DEVICE="${APP_FRAMEBUFFER_DEVICE}"
        APP_DRM_DEVICE="${APP_DRM_DEVICE}"
        APP_DRM_CONNECTOR_ID=${APP_DRM_CONNECTOR_ID}
        # This library owns camera_libcamera.cpp. The flag on lvgl does not
        # reach it, and without the flag the camera opens the unlinked stub.
        KVX_HAVE_LIBCAMERA=${KVX_HAVE_LIBCAMERA}
)

# ====================================================
# Model sources
# ====================================================
file(GLOB_RECURSE MODEL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model/*.cpp"
)

add_library(${PROJECT_NAME}_model
    STATIC
    ${MODEL_SOURCES}
)

target_include_directories(${PROJECT_NAME}_model
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/model"
)

# ====================================================
# View sources
# ====================================================
file(GLOB_RECURSE REACTIVE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reactive/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reactive/*.cpp"
)

add_library(${PROJECT_NAME}_reactive
    STATIC
    ${REACTIVE_SOURCES}
)

target_include_directories(${PROJECT_NAME}_reactive
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/reactive
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
)

target_link_libraries(${PROJECT_NAME}_reactive
    PUBLIC
        lvgl
)

file(GLOB_RECURSE TOOLS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/*.cpp"
)

add_library(${PROJECT_NAME}_tools
    STATIC
    ${TOOLS_SOURCES}
)

target_include_directories(${PROJECT_NAME}_tools
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tools"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/third_party"
)

# Camera session encodes JPEG/PNG and applies filters from tools.
# libcamera is linked directly (same approach as FXCam).
#
# BlueZ (libbluetooth): Wall Of Flipper/AirTag helper needs HCI headers + lib.
# CM0 BSP often ships only the runtime .so.N (no -dev symlink/headers).
# An empty normal variable makes find_path skip the search, so clear it first.
unset(BLUEZ_INCLUDE_DIR)
if(USE_DESKTOP)
    find_library(BLUEZ_LIBRARY NAMES bluetooth)
    find_path(BLUEZ_INCLUDE_DIR NAMES bluetooth/bluetooth.h)
else()
    cm0_find_library(BLUEZ_LIBRARY bluetooth libbluetooth.so.3 libbluetooth.so.3.19.15)
    cm0_find_header(BLUEZ_INCLUDE_DIR bluetooth/bluetooth.h
        "${CM0_SYSROOT}/usr/include"
    )
    if(NOT BLUEZ_INCLUDE_DIR)
        # Headers are arch-independent; stage a tiny include tree so we do not
        # pollute the cross compile with the whole host /usr/include.
        set(_bluez_hdr_cache "${CMAKE_CURRENT_SOURCE_DIR}/.cache/bluez-headers")
        set(_bluez_src_dir "")
        if(EXISTS "/usr/include/bluetooth/hci.h" AND EXISTS "/usr/include/bluetooth/hci_lib.h")
            set(_bluez_src_dir "/usr/include/bluetooth")
        endif()
        if(_bluez_src_dir STREQUAL "" AND CM0_ALLOW_FETCH_DEPS)
            message(STATUS "BlueZ headers missing in BSP; fetching libbluetooth-dev (amd64) for headers")
            set(_bluez_deb_cache "${CMAKE_CURRENT_SOURCE_DIR}/.cache/bluez-dev")
            file(MAKE_DIRECTORY "${_bluez_deb_cache}")
            execute_process(
                COMMAND bash -c "cd '${_bluez_deb_cache}' && apt-get download libbluetooth-dev 2>/dev/null || true"
                RESULT_VARIABLE _bluez_dl_rc
            )
            file(GLOB _bluez_debs "${_bluez_deb_cache}/libbluetooth-dev_*.deb")
            if(_bluez_debs)
                list(GET _bluez_debs 0 _bluez_deb)
                execute_process(
                    COMMAND dpkg-deb -x "${_bluez_deb}" "${_bluez_deb_cache}/root"
                    RESULT_VARIABLE _bluez_x_rc
                )
                if(EXISTS "${_bluez_deb_cache}/root/usr/include/bluetooth/hci.h")
                    set(_bluez_src_dir "${_bluez_deb_cache}/root/usr/include/bluetooth")
                endif()
            endif()
        endif()
        if(NOT _bluez_src_dir STREQUAL "")
            file(MAKE_DIRECTORY "${_bluez_hdr_cache}/bluetooth")
            foreach(_h IN ITEMS bluetooth.h hci.h hci_lib.h)
                file(COPY "${_bluez_src_dir}/${_h}" DESTINATION "${_bluez_hdr_cache}/bluetooth")
            endforeach()
            set(BLUEZ_INCLUDE_DIR "${_bluez_hdr_cache}")
            message(STATUS "Using staged BlueZ headers: ${BLUEZ_INCLUDE_DIR}")
        endif()
    endif()
endif()
if(NOT BLUEZ_LIBRARY)
    message(FATAL_ERROR
        "libbluetooth not found (needed for BLE advert sniff / Wall of Flipper+Airtag).\n"
        "Desktop: install bluez / libbluetooth. CM0: ensure libbluetooth.so.3 is in ${CM0_SYSROOT}."
    )
endif()
if(NOT BLUEZ_INCLUDE_DIR)
    message(FATAL_ERROR
        "bluetooth/*.h not found (need bluetooth.h, hci.h, hci_lib.h).\n"
        "Install libbluetooth-dev on the build host, or place headers under the BSP sysroot."
    )
endif()

target_link_libraries(${PROJECT_NAME}_platform
    PUBLIC
        ${PROJECT_NAME}_tools
        ${BLUEZ_LIBRARY}
)
target_include_directories(${PROJECT_NAME}_platform
    PUBLIC
        "${BLUEZ_INCLUDE_DIR}"
)
if(USE_DESKTOP)
    target_link_libraries(${PROJECT_NAME}_platform
        PUBLIC
            PNG::PNG
            ${JPEG_TARGET}
    )
else()
    target_link_libraries(${PROJECT_NAME}_platform
        PUBLIC
            CM0::PNG
            CM0::JPEG
    )
    if(KVX_HAVE_LIBCAMERA)
        target_link_libraries(${PROJECT_NAME}_platform
            PUBLIC
                CM0::libcamera
                CM0::libcamera_base
        )
        # Force -I ahead of the BSP sysroot's libcamera 0.7 headers so we
        # compile against the same 0.8 API we link (FXCam/App Center path).
        target_include_directories(${PROJECT_NAME}_platform
            BEFORE PUBLIC
                "${LIBCAMERA_INCLUDE_DIR}"
        )
        target_compile_options(${PROJECT_NAME}_platform
            PUBLIC
                "-I${LIBCAMERA_INCLUDE_DIR}"
        )
    endif()
endif()

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/view/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/view/*.cpp"  
)

add_library(${PROJECT_NAME}_view
    STATIC
    ${UI_SOURCES}
)

target_include_directories(${PROJECT_NAME}_view
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/screens
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/reactive
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tools
)

target_link_libraries(${PROJECT_NAME}_view
    PUBLIC
        lvgl
        ${PROJECT_NAME}_reactive
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_tools
)

# ====================================================
# ViewModel sources
# ====================================================
file(GLOB_RECURSE VIEWMODEL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/*.cpp"
)

add_library(${PROJECT_NAME}_viewmodel
    STATIC
    ${VIEWMODEL_SOURCES}
)

target_include_directories(${PROJECT_NAME}_viewmodel
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/model"
        ${LVGL_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME}_viewmodel
    PUBLIC
        lvgl
        ${PROJECT_NAME}_reactive
        ${PROJECT_NAME}_model
        ${PROJECT_NAME}_platform
)


# ====================================================
#   Core sources
# - Application lifecycle management
# - UI screen management
# - UI asset management
# - Reactive framework based on lvgl observer mode
# - Logger based on fmt
# ====================================================
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp"
)

file(GLOB_RECURSE LOGGER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/logger/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/logger/*.cpp"
)

add_library(logger STATIC
    ${LOGGER_SOURCES}
)

target_include_directories(logger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logger
    ${PLATFORM_INCLUDE_DIR}
)

target_link_libraries(logger
    PUBLIC
        fmt::fmt
    PRIVATE
        ${PLATFORM_LIBS}
)

set(CORE_GUI_PLATFORM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/linux_input.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/screenshot_service.cpp"
)

add_library(${PROJECT_NAME}_core STATIC
    ${APP_SOURCES}
    ${CORE_GUI_PLATFORM_SOURCES}
)

target_include_directories(${PROJECT_NAME}_core
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/logger
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/screens
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tools
)

target_compile_definitions(${PROJECT_NAME}_core
    PUBLIC
        APP_NAME="${APP_NAME}"
        APP_CONFIG_FILE="${APP_CONFIG_FILE}"
        APP_CMAKE_ASSETS_ROOT="${APP_ASSETS_ROOT}"
        APP_SOURCE_ASSETS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        APP_KEY_INPUT_DEVICE="${APP_KEY_INPUT_DEVICE}"
        APP_FRAMEBUFFER_DEVICE="${APP_FRAMEBUFFER_DEVICE}"
        APP_DRM_DEVICE="${APP_DRM_DEVICE}"
        APP_DRM_CONNECTOR_ID=${APP_DRM_CONNECTOR_ID}
)

target_link_libraries(${PROJECT_NAME}_core
    PUBLIC
        lvgl
        logger
        ${PROJECT_NAME}_view
        ${PROJECT_NAME}_viewmodel
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_tools
)

if(USE_DESKTOP)
    target_link_libraries(${PROJECT_NAME}_core
        PRIVATE
            PNG::PNG
    )
    if(TARGET SDL2::SDL2)
        target_link_libraries(${PROJECT_NAME}_core PRIVATE SDL2::SDL2)
    else()
        target_include_directories(${PROJECT_NAME}_core PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(${PROJECT_NAME}_core PRIVATE ${SDL2_LIBRARIES})
    endif()
endif()

# ====================================================
# Runners — shared tool work for GUI screens and CLI
# ====================================================
file(GLOB_RECURSE RUNNER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runners/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runners/*.cpp"
)

add_library(${PROJECT_NAME}_runners STATIC
    ${RUNNER_SOURCES}
)

target_include_directories(${PROJECT_NAME}_runners
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runners"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/model"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tools"
)

target_link_libraries(${PROJECT_NAME}_runners
    PUBLIC
        ${PROJECT_NAME}_model
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_tools
)

# ====================================================
# main
# ====================================================
add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_core
        ${PROJECT_NAME}_model
        ${PROJECT_NAME}_view
        ${PROJECT_NAME}_viewmodel
        ${PROJECT_NAME}_runners
)

# On-device Cap ST25 test harness (same platform code as the UI).
add_executable(${PROJECT_NAME}-nfctest
    src/nfctest_main.cpp
)

target_link_libraries(${PROJECT_NAME}-nfctest
    PRIVATE
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_core
        ${PROJECT_NAME}_model
        ${PROJECT_NAME}_view
        ${PROJECT_NAME}_viewmodel
)

# Privileged Wi-Fi monitor helper (file capabilities, not setuid).
add_executable(${PROJECT_NAME}-wifimon
    src/helpers/wifimon_main.cpp
)

# Privileged BLE advert helper (raw AD payload for Wall of Flipper / AirTag).
add_executable(${PROJECT_NAME}-blescan
    src/helpers/blescan_main.cpp
)
target_include_directories(${PROJECT_NAME}-blescan PRIVATE "${BLUEZ_INCLUDE_DIR}")
target_link_libraries(${PROJECT_NAME}-blescan PRIVATE ${BLUEZ_LIBRARY})

# SSH / TTY CLI — no LVGL, SDL, view, or core.
file(GLOB_RECURSE CLI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/*.cpp"
)

add_executable(${PROJECT_NAME}-cli
    ${CLI_SOURCES}
)

target_include_directories(${PROJECT_NAME}-cli
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runners"
)

target_link_libraries(${PROJECT_NAME}-cli
    PRIVATE
        ${PROJECT_NAME}_runners
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_tools
        ${PROJECT_NAME}_model
        pthread
)

if(USE_DESKTOP)
    add_executable(camera_filter_selftest
        src/helpers/camera_filter_selftest.cpp
        src/tools/camera_filter.cpp
    )
    target_include_directories(camera_filter_selftest
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/tools"
    )

    add_executable(camera_session_smoke
        src/helpers/camera_session_smoke.cpp
    )
    target_link_libraries(camera_session_smoke
        PRIVATE
            ${PROJECT_NAME}_platform
            ${PROJECT_NAME}_tools
            PNG::PNG
            ${JPEG_TARGET}
            pthread
    )
endif()

# Package deb


include(cmake/cm0-package.cmake)
