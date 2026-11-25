#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
# Packaging and install layout for CardputerZero Debian builds.

include(GNUInstallDirs)

set(APP_DISPLAY_NAME "kvxputerzero")
set(APP_DEBIAN_REVISION "rev11" CACHE STRING "Debian package revision/vendor suffix")
set(APP_DEBIAN_ARCHITECTURE "arm64" CACHE STRING "Debian package architecture")
# Deliberately a placeholder: the store rejects packages whose Maintainer is
# still a template/M5Stack identity, so set your own name and email here.
set(APP_MAINTAINER "kvx <kvx@users.noreply.github.com>")
set(APP_PACKAGE_DESCRIPTION "kvxputer toolkit shell for Cardputer Zero")
set(APP_INSTALL_SYSTEMD_SERVICE ON CACHE BOOL "Install a systemd service file for embedded deployments")

set(APP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/package")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/app.desktop.in"
    "${APP_GENERATED_DIR}/${PROJECT_NAME}.desktop"
    @ONLY
)

if(APP_INSTALL_SYSTEMD_SERVICE)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/templates/app.service.in"
        "${APP_GENERATED_DIR}/${PROJECT_NAME}.service"
        @ONLY
    )
endif()

set(_cm0_runtime_targets ${PROJECT_NAME} ${PROJECT_NAME}-nfctest)
if(TARGET ${PROJECT_NAME}-cli)
    list(APPEND _cm0_runtime_targets ${PROJECT_NAME}-cli)
endif()
install(TARGETS ${_cm0_runtime_targets}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS ${PROJECT_NAME}-wifimon
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/kvxputerzero
)
install(TARGETS ${PROJECT_NAME}-blescan
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/kvxputerzero
)
install(TARGETS ${PROJECT_NAME}-hidgadget
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/kvxputerzero
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/kvxputerzero.conf"
    DESTINATION "/etc"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/audio"
    PATTERN ".DS_Store" EXCLUDE
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/fonts"
    PATTERN ".DS_Store" EXCLUDE
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/images"
    PATTERN ".DS_Store" EXCLUDE
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/pcconnect/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/pcconnect"
    PATTERN ".DS_Store" EXCLUDE
)
# Generated MAC vendor table (built by the oui_table target; not the raw CSV).
if(DEFINED OUI_TABLE_BIN)
    install(FILES "${OUI_TABLE_BIN}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/files"
    )
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/share/images"
    FILES_MATCHING
    PATTERN "kvxputerzero*.png"
)

install(FILES "${APP_GENERATED_DIR}/${PROJECT_NAME}.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/applications"
)

if(APP_INSTALL_SYSTEMD_SERVICE)
    install(FILES "${APP_GENERATED_DIR}/${PROJECT_NAME}.service"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/system"
    )
endif()


install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE.txt"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    RENAME "third-party-assets-license.txt"
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(CPACK_PACKAGE_NAME "${APP_DISPLAY_NAME}")
set(CPACK_PACKAGE_VENDOR "${APP_MAINTAINER}")
set(CPACK_PACKAGE_CONTACT "${APP_MAINTAINER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${APP_PACKAGE_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${APP_DISPLAY_NAME}_${PROJECT_VERSION}_${APP_DEBIAN_REVISION}_${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE.txt")

string(TOLOWER "${APP_DISPLAY_NAME}" APP_DEBIAN_PACKAGE_NAME)
string(REGEX REPLACE "[^a-z0-9+.-]" "-" APP_DEBIAN_PACKAGE_NAME "${APP_DEBIAN_PACKAGE_NAME}")
set(CPACK_DEBIAN_PACKAGE_NAME "${APP_DEBIAN_PACKAGE_NAME}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${APP_DEBIAN_REVISION}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${APP_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
# Same model as FXCam App Center packages: soft OR-chain so apt accepts the
# libcamera already on the Cardputer (do not force a mismatched IPA). Stock
# images already ship libcamera0.x + libcamera-ipa — no extra user installs.
set(APP_DEBIAN_DEPENDS "libc6, libstdc++6, libgcc-s1, libfreetype6, libpng16-16, libjpeg62-turbo, zlib1g, iw, iproute2, libcap2-bin, libbluetooth3, libcamera0.8 | libcamera0.7 | libcamera0.6 | libcamera0.5 | libcamera0.4 | libcamera0.3 | libcamera0.2 | libcamera0, libcamera-ipa")
if(FMT_INCLUDE_DIR AND FMT_LIBRARY)
    string(APPEND APP_DEBIAN_DEPENDS ", libfmt10")
endif()
set(CPACK_DEBIAN_PACKAGE_DEPENDS "${APP_DEBIAN_DEPENDS}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)

foreach(_ver_tgt ${PROJECT_NAME}_view ${PROJECT_NAME}_runners)
    if(TARGET ${_ver_tgt})
        target_compile_definitions(${_ver_tgt}
            PUBLIC
                KVX_APP_VERSION="${PROJECT_VERSION}"
                KVX_APP_REVISION="${APP_DEBIAN_REVISION}"
        )
    endif()
endforeach()

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/conffiles.in"
    "${APP_GENERATED_DIR}/conffiles"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/postinst.in"
    "${APP_GENERATED_DIR}/postinst"
    @ONLY
)
file(CHMOD "${APP_GENERATED_DIR}/postinst" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${APP_GENERATED_DIR}/conffiles;${APP_GENERATED_DIR}/postinst")

include(CPack)
