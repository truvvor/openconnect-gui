if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(MINGW_VARIANT MINGW64)
else()
    set(MINGW_VARIANT MINGW32)
endif()

# --------------------------------------------------------------------------------------------------
# openconnect (camouflage build, sourced from local external/ zip first; fall back to upstream
# horar/openconnect 8.02 only when no camouflage zip is available)
# --------------------------------------------------------------------------------------------------
set(OC_CAMO_RT  "${CMAKE_SOURCE_DIR}/external/openconnect-v${openconnect-TAG}-camouflage_${MINGW_VARIANT}.zip")
set(OC_CAMO_DEV "${CMAKE_SOURCE_DIR}/external/openconnect-devel-v${openconnect-TAG}-camouflage_${MINGW_VARIANT}.zip")

if(EXISTS ${OC_CAMO_RT} AND EXISTS ${OC_CAMO_DEV})
    message(STATUS "openconnect: using LOCAL camouflage zips for ${MINGW_VARIANT}")
    set(OPENCONNECT_DEV_URL ${OC_CAMO_DEV})
    set(OPENCONNECT_URL     ${OC_CAMO_RT})
    set(OC_USING_CAMOUFLAGE 1)
else()
    message(STATUS "openconnect: falling back to upstream horar/openconnect ${openconnect-TAG} prebuilt (no camouflage)")
    set(OPENCONNECT_DEV_URL https://github.com/horar/openconnect/releases/download/${openconnect-TAG}/openconnect-devel-${openconnect-TAG}_${MINGW_VARIANT}.zip)
    set(OPENCONNECT_URL     https://github.com/horar/openconnect/releases/download/${openconnect-TAG}/openconnect-${openconnect-TAG}_${MINGW_VARIANT}.zip)
    set(OC_USING_CAMOUFLAGE 0)
endif()

# devel: headers + .dll.a import libs
ExternalProject_Add(openconnect-devel-${openconnect-TAG}
    PREFIX ${CMAKE_BINARY_DIR}/external
    INSTALL_DIR ${CMAKE_BINARY_DIR}/external
    DOWNLOAD_NO_PROGRESS 1
    URL ${OPENCONNECT_DEV_URL}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

if(OC_USING_CAMOUFLAGE)
    # camouflage zip layout: include/ and lib/ at root
    ExternalProject_Add_Step(openconnect-devel-${openconnect-TAG} deploy_headers
        COMMAND ${CMAKE_COMMAND} -E copy_directory include <INSTALL_DIR>/include
        COMMENT "...deploing openconnect headers (camouflage)"
        WORKING_DIRECTORY <SOURCE_DIR>
        DEPENDEES install
    )
    ExternalProject_Add_Step(openconnect-devel-${openconnect-TAG} deploy_libraries
        COMMAND ${CMAKE_COMMAND} -E copy_directory lib <INSTALL_DIR>/lib
        COMMENT "...deploing openconnect import libs (camouflage)"
        WORKING_DIRECTORY <SOURCE_DIR>
        DEPENDEES install
        ALWAYS 0
    )
else()
    # legacy horar zip layout: include/ and lib/ at root
    ExternalProject_Add_Step(openconnect-devel-${openconnect-TAG} deploy_headers
        COMMAND ${CMAKE_COMMAND} -E copy_directory include <INSTALL_DIR>/include
        COMMENT "...deploing openconnect-${openconnect-TAG} headers"
        WORKING_DIRECTORY <SOURCE_DIR>
        DEPENDEES install
    )
    ExternalProject_Add_Step(openconnect-devel-${openconnect-TAG} deploy_libraries
        COMMAND ${CMAKE_COMMAND} -E copy_directory lib <INSTALL_DIR>/lib
        COMMENT "...deploing openconnect-${openconnect-TAG} libraries"
        WORKING_DIRECTORY <SOURCE_DIR>
        DEPENDEES install
        ALWAYS 0
    )
endif()

# runtime: openconnect.exe + libopenconnect-5.dll + transitive DLLs + vpnc-script-win.js
ExternalProject_Add(openconnect-${openconnect-TAG}
    PREFIX ${CMAKE_BINARY_DIR}/external
    INSTALL_DIR ${CMAKE_BINARY_DIR}/external
    DOWNLOAD_NO_PROGRESS 1
    URL ${OPENCONNECT_URL}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

if(OC_USING_CAMOUFLAGE)
    # camouflage zip puts everything at root
    ExternalProject_Add_Step(openconnect-${openconnect-TAG} deploy_libs
        COMMAND ${CMAKE_COMMAND} -E copy_directory . <INSTALL_DIR>/lib
        COMMENT "...deploing openconnect runtime DLLs (camouflage)"
        WORKING_DIRECTORY <SOURCE_DIR>
        DEPENDEES install
    )
else()
    ExternalProject_Add_Step(openconnect-${openconnect-TAG} deploy_libs
        COMMAND ${CMAKE_COMMAND} -E copy_directory . <INSTALL_DIR>/lib
        COMMENT "...deploing openconnect-${openconnect-TAG} libraries"
        WORKING_DIRECTORY <SOURCE_DIR>
        DEPENDEES install
    )
endif()

# Imported targets.
# NOTE: we only declare *targets that openconnect-gui src/ actually links against*
# and let install copy whatever DLLs end up in external/lib/ at packaging time.
add_executable(openconnect::app IMPORTED)
set_property(TARGET openconnect::app PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/lib/openconnect.exe)

add_library(openconnect::openconnect SHARED IMPORTED)
set_property(TARGET openconnect::openconnect PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/lib/libopenconnect-5.dll)
set_property(TARGET openconnect::openconnect PROPERTY IMPORTED_IMPLIB ${CMAKE_BINARY_DIR}/external/lib/libopenconnect.dll.a)

# Legacy IMPORTED targets used by src/CMakeLists.txt are kept as harmless stubs.
# We resolve real DLLs at install time via the glob below.
# stoken removed from the loop below: MSYS2 dropped mingw-w64-i686-stoken
# in 2025 so we can't rely on the .dll.a being present. src/CMakeLists.txt
# links it conditionally via ${HAVE_STOKEN}.
foreach(_lib gmp gnutls hogweed nettle p11-kit xml2)
    add_library(openconnect::${_lib} SHARED IMPORTED)
    # Best-effort: try to locate import lib by exact name
    set(_implib ${CMAKE_BINARY_DIR}/external/lib/lib${_lib}.dll.a)
    set_property(TARGET openconnect::${_lib} PROPERTY IMPORTED_IMPLIB ${_implib})
    # IMPORTED_LOCATION is required for SHARED IMPORTED; point to libopenconnect-5.dll as
    # a placeholder — actual DLL gets installed by the glob below
    set_property(TARGET openconnect::${_lib} PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/lib/libopenconnect-5.dll)
endforeach()

# stoken is optional. Decide at *configure* time based on the target ABI:
# MSYS2 stopped shipping mingw-w64-i686-stoken in 2025, so we only assume it
# on 64-bit builds. build-openconnect.yml also skips packaging its .dll.a
# into the MINGW32 devel zip on that side, so keeping this consistent avoids
# a fake HAVE_STOKEN=1 that would blow up at link time.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_library(openconnect::stoken SHARED IMPORTED)
    set_property(TARGET openconnect::stoken PROPERTY IMPORTED_IMPLIB ${CMAKE_BINARY_DIR}/external/lib/libstoken.dll.a)
    set_property(TARGET openconnect::stoken PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/lib/libopenconnect-5.dll)
    set(HAVE_STOKEN 1 CACHE INTERNAL "stoken linked (x64)")
else()
    set(HAVE_STOKEN 0 CACHE INTERNAL "stoken not linked (x86 — MSYS2 dropped the package)")
endif()

# Console install: openconnect.exe + vpnc-script
install(
    FILES
        ${CMAKE_BINARY_DIR}/external/lib/openconnect.exe
        ${CMAKE_BINARY_DIR}/external/lib/vpnc-script-win.js
    DESTINATION .
    COMPONENT App_Console
)

# Install ALL DLLs from external/lib/ into the package — covers gnutls and all transitive deps
# regardless of soname version (libhogweed-4 vs -6, libnettle-6 vs -8, libxml2-2 vs -16 etc.)
install(
    DIRECTORY ${CMAKE_BINARY_DIR}/external/lib/
    DESTINATION .
    COMPONENT App
    FILES_MATCHING
        PATTERN "*.dll"
        PATTERN "include" EXCLUDE
)
