cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

# Keenetic anti-DPI VPN client — fork of openconnect-gui.
# Windows-only single-protocol client for our patched ocserv.
# Upstream macOS/Linux/Apple-specific configuration removed.

project(keenetic-vpn-gui
    VERSION 1.0.0
    LANGUAGES CXX
)

message(STATUS "Building ${PROJECT_NAME} v${PROJECT_VERSION} [Keenetic anti-DPI fork]")

set(PRODUCT_NAME_SHORT "Keenetic-VPN")
set(PRODUCT_NAME_LONG "Keenetic anti-DPI VPN client")
set(PRODUCT_NAME_COMPANY "Keenetic anti-DPI Team")
set(PRODUCT_NAME_COMPANY_DOMAIN "github.io")
set(PRODUCT_NAME_COPYRIGHT_SHORT "Copyright (C) 2026 Keenetic anti-DPI Team")
set(PRODUCT_NAME_COPYRIGHT_FULL "Copyright (C) 2014-2015 Red Hat Inc.<br>Copyright (C) 2016-2019 OpenConnect-GUI Team<br>${PRODUCT_NAME_COPYRIGHT_SHORT}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake/Modules/")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake/Includes/")

include(ProjectSettings)
include(ProjectDependencies)

# nsis/ subdir removed — replaced with WiX MSI in installer/ (Phase 5).
add_subdirectory(src)

# WinTun service (Phase 3) — privileged helper that owns the WinTun adapter
# so the GUI doesn't need UAC at every launch. Built only on Windows.
if(WIN32)
    add_subdirectory(wintun-service)
    add_subdirectory(installer)
endif()

install(FILES LICENSE.txt
    DESTINATION .
    COMPONENT App
)

include(clang-format)
include(git_revision_main)
include(build_timestamp_main)

include(ProjectPackage)

