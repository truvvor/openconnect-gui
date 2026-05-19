# installer/CMakeLists.txt — WiX MSI build.
#
# Requires:
#   WiX 3.11+ (candle + light) on PATH.
#   The GUI + service exes already built in ${CMAKE_BINARY_DIR}.
#   The openconnect-keenetic zip already unpacked (Phase 1).
#
# Produces:
#   ${CMAKE_BINARY_DIR}/Keenetic-VPN-${PROJECT_VERSION}-x64.msi
#
# Signing (optional):
#   Set env-vars KEENETIC_SIGN_PFX (.pfx path) + KEENETIC_SIGN_PASS before
#   calling cmake --build. signtool runs after light.exe.

if(NOT WIN32)
    message(STATUS "Skipping installer/ on non-Windows host")
    return()
endif()

find_program(WIX_CANDLE candle.exe)
find_program(WIX_LIGHT  light.exe)
if(NOT WIX_CANDLE OR NOT WIX_LIGHT)
    message(WARNING "WiX (candle/light) not found in PATH — MSI build skipped. "
                    "Install with 'choco install wixtoolset' or download from "
                    "https://github.com/wixtoolset/wix3/releases.")
    return()
endif()

set(MSI_OUT "${CMAKE_BINARY_DIR}/Keenetic-VPN-${PROJECT_VERSION}-x64.msi")
set(WXS     "${CMAKE_CURRENT_SOURCE_DIR}/product.wxs")
set(WIXOBJ  "${CMAKE_CURRENT_BINARY_DIR}/product.wixobj")

set(BUILD_DIR   "${CMAKE_BINARY_DIR}/src")
set(OCDEPS_DIR  "${CMAKE_BINARY_DIR}/external/openconnect-keenetic/bin")
set(BUNDLE_DIR  "${CMAKE_SOURCE_DIR}/bundle")
set(WINTUN_DIR  "${CMAKE_SOURCE_DIR}/external/wintun-bin")

add_custom_command(
    OUTPUT  "${WIXOBJ}"
    DEPENDS "${WXS}" keenetic-vpn-gui KeeneticVpnService
    COMMAND "${WIX_CANDLE}"
            -arch x64
            -dBuildDir=${BUILD_DIR}
            -dOcDepsDir=${OCDEPS_DIR}
            -dBundleDir=${BUNDLE_DIR}
            -dWintunDir=${WINTUN_DIR}
            -out "${WIXOBJ}"
            "${WXS}"
    COMMENT "WiX: candle product.wxs"
)

add_custom_command(
    OUTPUT  "${MSI_OUT}"
    DEPENDS "${WIXOBJ}"
    COMMAND "${WIX_LIGHT}"
            -ext WixUIExtension
            -ext WixUtilExtension
            -out "${MSI_OUT}"
            "${WIXOBJ}"
    COMMAND ${CMAKE_COMMAND}
            -E env "$<$<BOOL:$ENV{KEENETIC_SIGN_PFX}>:signtool sign /fd SHA256 /f $ENV{KEENETIC_SIGN_PFX} /p $ENV{KEENETIC_SIGN_PASS} /t http://timestamp.digicert.com ${MSI_OUT}>"
    COMMENT "WiX: light → MSI (signed if KEENETIC_SIGN_PFX is set)"
)

add_custom_target(installer ALL DEPENDS "${MSI_OUT}")
