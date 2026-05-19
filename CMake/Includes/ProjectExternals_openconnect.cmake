# ProjectExternals_openconnect.cmake — Keenetic-camouflage fork.
#
# This fork builds libopenconnect itself (with our 4 patches) via
# scripts/build-libopenconnect-keenetic.sh in WSL/MinGW64. The script produces
# external/openconnect-keenetic_mingw64.zip which we unpack here and expose as
# imported targets.
#
# To regenerate the zip from scratch:
#     bash scripts/build-libopenconnect-keenetic.sh
#
# If the zip is missing the CMake configure will fail with a clear message.

set(KEENETIC_OC_ZIP "${CMAKE_SOURCE_DIR}/external/openconnect-keenetic_mingw64.zip")
set(KEENETIC_OC_STAGE "${CMAKE_BINARY_DIR}/external/openconnect-keenetic")

if(NOT EXISTS "${KEENETIC_OC_ZIP}")
    message(FATAL_ERROR
        "external/openconnect-keenetic_mingw64.zip is missing.\n"
        "Run:  bash scripts/build-libopenconnect-keenetic.sh\n"
        "(requires WSL Ubuntu 22.04 with mingw-w64 + win-iconv-mingw-w64-dev).\n")
endif()

if(MINGW AND NOT EXISTS "${KEENETIC_OC_STAGE}/bin/libopenconnect-5.dll")
    file(MAKE_DIRECTORY "${KEENETIC_OC_STAGE}")
    message(STATUS "Unpacking ${KEENETIC_OC_ZIP} -> ${KEENETIC_OC_STAGE}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xf "${KEENETIC_OC_ZIP}"
        WORKING_DIRECTORY "${KEENETIC_OC_STAGE}"
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Failed to unpack ${KEENETIC_OC_ZIP}")
    endif()
endif()

# Create a stub target so src/CMakeLists.txt's add_dependencies(... openconnect-keenetic)
# resolves even though we have no separate build step (unpack is done above).
if(NOT TARGET openconnect-keenetic)
    add_custom_target(openconnect-keenetic
        COMMENT "openconnect-keenetic_mingw64.zip already staged"
    )
endif()

if(MINGW)
    set(_KOC_BIN "${KEENETIC_OC_STAGE}/bin")
    set(_KOC_LIB "${KEENETIC_OC_STAGE}/lib")
    set(_KOC_INC "${KEENETIC_OC_STAGE}/include")

    add_library(openconnect::openconnect SHARED IMPORTED)
    set_target_properties(openconnect::openconnect PROPERTIES
        IMPORTED_LOCATION "${_KOC_BIN}/libopenconnect-5.dll"
        IMPORTED_IMPLIB   "${_KOC_LIB}/libopenconnect.dll.a"
        INTERFACE_INCLUDE_DIRECTORIES "${_KOC_INC}"
    )

    add_library(openconnect::gnutls SHARED IMPORTED)
    set_target_properties(openconnect::gnutls PROPERTIES
        IMPORTED_LOCATION "${_KOC_BIN}/libgnutls-30.dll"
        IMPORTED_IMPLIB   "${_KOC_LIB}/libgnutls.dll.a"
    )

    foreach(_dep gmp nettle hogweed p11-kit xml2)
        add_library(openconnect::${_dep} SHARED IMPORTED)
    endforeach()
    set_target_properties(openconnect::gmp      PROPERTIES IMPORTED_LOCATION "${_KOC_BIN}/libgmp-10.dll"   IMPORTED_IMPLIB "${_KOC_LIB}/libgmp.dll.a")
    set_target_properties(openconnect::nettle   PROPERTIES IMPORTED_LOCATION "${_KOC_BIN}/libnettle-6.dll" IMPORTED_IMPLIB "${_KOC_LIB}/libnettle.dll.a")
    set_target_properties(openconnect::hogweed  PROPERTIES IMPORTED_LOCATION "${_KOC_BIN}/libhogweed-4.dll" IMPORTED_IMPLIB "${_KOC_LIB}/libhogweed.dll.a")
    set_target_properties(openconnect::p11-kit  PROPERTIES IMPORTED_LOCATION "${_KOC_BIN}/libp11-kit-0.dll" IMPORTED_IMPLIB "${_KOC_LIB}/libp11-kit.dll.a")
    set_target_properties(openconnect::xml2     PROPERTIES IMPORTED_LOCATION "${_KOC_BIN}/libxml2-2.dll"    IMPORTED_IMPLIB "${_KOC_LIB}/libxml2.dll.a")

    # Install runtime DLLs to the GUI exe directory.
    install(DIRECTORY "${_KOC_BIN}/"
        DESTINATION .
        COMPONENT App
        FILES_MATCHING PATTERN "*.dll"
    )
endif()

# tag for downstream consumers; keep historical name for CMake string lookups.
set(openconnect-TAG keenetic-camouflage)
