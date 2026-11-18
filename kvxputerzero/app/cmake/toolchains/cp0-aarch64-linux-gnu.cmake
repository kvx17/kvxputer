#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#
# CardputerZero aarch64 cross toolchain.
# The BSP under .cache/sdk_bsp-src is a sysroot: it contains target headers,
# target libraries, startup objects, and pkg-config metadata, but no CMake project.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CM0_MULTIARCH aarch64-linux-gnu CACHE STRING "Target Debian multiarch tuple")

get_filename_component(_CP0_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_CP0_CACHE_DIR "${_CP0_PROJECT_ROOT}/.cache")
set(_CP0_DEFAULT_SYSROOT "${_CP0_CACHE_DIR}/sdk_bsp-src")

set(CM0_SDK_ROOT "${_CP0_DEFAULT_SYSROOT}" CACHE PATH "Path to CM0/CardputerZero BSP sysroot")
set(CM0_SDK_VERSION "v0.0.4" CACHE STRING "CM0/CardputerZero BSP release version")
set(CM0_SDK_URL "https://github.com/CardputerZero/M5CardputerZero-UserDemo/releases/download/${CM0_SDK_VERSION}/sdk_bsp.tar.gz" CACHE STRING "CM0/CardputerZero BSP archive URL")
set(CM0_ALLOW_FETCH_DEPS ON CACHE BOOL "Allow downloading the CM0/CardputerZero BSP when the sysroot is missing")

# CMAKE_SYSROOT must exist before project() runs, otherwise CMake's compiler ABI
# checks fail before the main CMakeLists.txt has a chance to download anything.
if(NOT EXISTS "${CM0_SDK_ROOT}")
    if(NOT CM0_ALLOW_FETCH_DEPS)
        message(FATAL_ERROR
            "CM0 SDK sysroot not found: ${CM0_SDK_ROOT}\n"
            "Enable -DCM0_ALLOW_FETCH_DEPS=ON or provide -DCM0_SDK_ROOT=<path>."
        )
    endif()

    file(MAKE_DIRECTORY "${_CP0_CACHE_DIR}")
    get_filename_component(_CP0_SYSROOT_PARENT "${CM0_SDK_ROOT}" DIRECTORY)
    file(MAKE_DIRECTORY "${_CP0_SYSROOT_PARENT}")
    set(_CP0_SDK_ARCHIVE "${_CP0_CACHE_DIR}/sdk_bsp.tar.gz")

    message(STATUS "CM0 SDK sysroot missing, downloading BSP: ${CM0_SDK_URL}")
    file(DOWNLOAD
        "${CM0_SDK_URL}"
        "${_CP0_SDK_ARCHIVE}"
        SHOW_PROGRESS
        STATUS _CP0_DOWNLOAD_STATUS
        TIMEOUT 300
    )
    list(GET _CP0_DOWNLOAD_STATUS 0 _CP0_DOWNLOAD_CODE)
    list(GET _CP0_DOWNLOAD_STATUS 1 _CP0_DOWNLOAD_MESSAGE)
    if(NOT _CP0_DOWNLOAD_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download CM0 BSP: ${_CP0_DOWNLOAD_MESSAGE}")
    endif()

    message(STATUS "Extracting CM0 BSP to ${CM0_SDK_ROOT}")
    file(MAKE_DIRECTORY "${CM0_SDK_ROOT}")
    file(ARCHIVE_EXTRACT INPUT "${_CP0_SDK_ARCHIVE}" DESTINATION "${CM0_SDK_ROOT}")
endif()

if(NOT EXISTS "${CM0_SDK_ROOT}/usr/include" OR NOT EXISTS "${CM0_SDK_ROOT}/usr/lib")
    file(GLOB _CP0_EXTRACTED_DIRS LIST_DIRECTORIES true "${CM0_SDK_ROOT}/*")
    foreach(_CP0_EXTRACTED_DIR IN LISTS _CP0_EXTRACTED_DIRS)
        if(EXISTS "${_CP0_EXTRACTED_DIR}/usr/include" AND EXISTS "${_CP0_EXTRACTED_DIR}/usr/lib")
            set(CM0_SDK_ROOT "${_CP0_EXTRACTED_DIR}" CACHE PATH "Path to CM0/CardputerZero BSP sysroot" FORCE)
            message(STATUS "Using extracted CM0 sysroot: ${CM0_SDK_ROOT}")
            break()
        endif()
    endforeach()
endif()

if(NOT EXISTS "${CM0_SDK_ROOT}/usr/include" OR NOT EXISTS "${CM0_SDK_ROOT}/usr/lib")
    message(FATAL_ERROR
        "CM0 SDK sysroot appears incomplete: ${CM0_SDK_ROOT}\n"
        "Expected usr/include and usr/lib after extracting sdk_bsp.tar.gz."
    )
endif()

set(CMAKE_SYSROOT "${CM0_SDK_ROOT}" CACHE PATH "Sysroot used for CardputerZero cross builds" FORCE)
set(CMAKE_SYSROOT_COMPILE "${CMAKE_SYSROOT}" CACHE PATH "Compile sysroot used for CardputerZero cross builds" FORCE)
set(CMAKE_SYSROOT_LINK "${CMAKE_SYSROOT}" CACHE PATH "Link sysroot used for CardputerZero cross builds" FORCE)

find_program(_CP0_C_COMPILER NAMES aarch64-linux-gnu-gcc REQUIRED)
find_program(_CP0_CXX_COMPILER NAMES aarch64-linux-gnu-g++ REQUIRED)
set(CMAKE_C_COMPILER "${_CP0_C_COMPILER}" CACHE FILEPATH "aarch64 Linux C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${_CP0_CXX_COMPILER}" CACHE FILEPATH "aarch64 Linux C++ compiler" FORCE)
set(CMAKE_LIBRARY_ARCHITECTURE "${CM0_MULTIARCH}")

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}" CACHE STRING "Root paths for cross find_* calls" FORCE)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

list(PREPEND CMAKE_PREFIX_PATH
    "${CMAKE_SYSROOT}/usr"
    "${CMAKE_SYSROOT}/usr/lib/${CM0_MULTIARCH}/cmake"
    "${CMAKE_SYSROOT}/usr/local"
)

# Make pkg-config resolve BSP metadata as target paths instead of host paths.
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}
    "${CMAKE_SYSROOT}/usr/lib/${CM0_MULTIARCH}/pkgconfig:${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig"
)

# The BSP includes GCC runtime/startup files. Use them during link tests and
# final links so crt*.o/libstdc++/libgcc are resolved from the sysroot first.
file(GLOB _CP0_GCC_RUNTIME_DIRS LIST_DIRECTORIES true "${CMAKE_SYSROOT}/usr/lib/gcc/${CM0_MULTIARCH}/*")
set(_CP0_MULTIARCH_LIB_DIR "${CMAKE_SYSROOT}/usr/lib/${CM0_MULTIARCH}")
if(_CP0_GCC_RUNTIME_DIRS)
    list(SORT _CP0_GCC_RUNTIME_DIRS COMPARE NATURAL ORDER DESCENDING)
    list(GET _CP0_GCC_RUNTIME_DIRS 0 CP0_GCC_RUNTIME_DIR)
    set(CP0_GCC_RUNTIME_DIR "${CP0_GCC_RUNTIME_DIR}" CACHE PATH "GCC runtime directory inside CM0 sysroot")
    set(_CP0_SYSROOT_LINK_FLAGS
        "-B${_CP0_MULTIARCH_LIB_DIR}/ -B${CP0_GCC_RUNTIME_DIR}/ -Wl,-rpath-link,${_CP0_MULTIARCH_LIB_DIR} -L${_CP0_MULTIARCH_LIB_DIR}"
    )
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT} ${_CP0_SYSROOT_LINK_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_SHARED_LINKER_FLAGS_INIT} ${_CP0_SYSROOT_LINK_FLAGS}")
endif()

# Arch Linux (and similar) aarch64-linux-gnu toolchains are configured with
# --with-sysroot=/usr/aarch64-linux-gnu. Passing CMAKE_SYSROOT=<BSP> then still
# pulls host cross C/C++ headers from /usr/aarch64-linux-gnu/include while the
# Template also adds -I${BSP}/usr/include/aarch64-linux-gnu for multiarch bits/.
# That mix breaks <cmath> (__intmax_t / mathcalls.h). Force BSP headers instead.
set(_CP0_MULTIARCH_INC "${CMAKE_SYSROOT}/usr/include/${CM0_MULTIARCH}")
set(_CP0_USR_INC "${CMAKE_SYSROOT}/usr/include")
file(GLOB _CP0_LIBSTDCPP_DIRS LIST_DIRECTORIES true "${_CP0_USR_INC}/c++/*")
if(_CP0_LIBSTDCPP_DIRS AND EXISTS "${_CP0_MULTIARCH_INC}")
    list(SORT _CP0_LIBSTDCPP_DIRS COMPARE NATURAL ORDER DESCENDING)
    list(GET _CP0_LIBSTDCPP_DIRS 0 _CP0_LIBSTDCPP_DIR)
    get_filename_component(_CP0_LIBSTDCPP_VER "${_CP0_LIBSTDCPP_DIR}" NAME)
    set(_CP0_LIBSTDCPP_TARGET_DIR "${_CP0_MULTIARCH_INC}/c++/${_CP0_LIBSTDCPP_VER}")
    if(NOT EXISTS "${_CP0_LIBSTDCPP_TARGET_DIR}")
        set(_CP0_LIBSTDCPP_TARGET_DIR "${_CP0_LIBSTDCPP_DIR}/${CM0_MULTIARCH}")
    endif()
    set(_CP0_FORCE_C_INCLUDES
        "-isystem ${_CP0_MULTIARCH_INC} -isystem ${_CP0_USR_INC}"
    )
    set(_CP0_FORCE_CXX_INCLUDES
        "-nostdinc++ -isystem ${_CP0_LIBSTDCPP_DIR} -isystem ${_CP0_LIBSTDCPP_TARGET_DIR} ${_CP0_FORCE_C_INCLUDES}"
    )
    set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} ${_CP0_FORCE_C_INCLUDES}")
    set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${_CP0_FORCE_CXX_INCLUDES}")
    message(STATUS "Forcing BSP C/C++ includes (libstdc++ ${_CP0_LIBSTDCPP_VER}) for Arch-style cross-gcc")
endif()
