# wintun-service — privileged Windows service that owns the WinTun adapter.
# Built only on Windows (MinGW-w64).

if(NOT WIN32)
    message(STATUS "Skipping wintun-service on non-Windows host")
    return()
endif()

add_executable(KeeneticVpnService
    service.cpp
    pipe_server.cpp
    wintun_adapter.cpp
)

target_include_directories(KeeneticVpnService PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(KeeneticVpnService PRIVATE
    _WIN32_WINNT=0x0A00          # Windows 10+
    UNICODE _UNICODE
)

target_link_libraries(KeeneticVpnService PRIVATE
    advapi32      # SCM, SDDL, registry
    iphlpapi      # routing-table / interface API
    ws2_32        # winsock for any future packet path
)

# Install service exe + WinTun runtime dll alongside.
install(TARGETS KeeneticVpnService
    RUNTIME DESTINATION . COMPONENT Service
)

# WinTun.dll is fetched/bundled by the MSI installer (installer/), not here.
