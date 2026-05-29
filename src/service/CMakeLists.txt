# openconnect-gui-service — privileged VPN service (Windows-only).
#
# Console-subsystem app (NOT WIN32_EXECUTABLE) so it can run under the SCM and,
# with --console, in the foreground. Q_OBJECT classes here rely on the global
# AUTOMOC (ProjectDependencies.cmake). The VPN engine + its openconnect link set
# are added in P3/P4; P2 is the skeleton (SCM + pipe + handshake).

set(SERVICE_TARGET openconnect-gui-service)

add_executable(${SERVICE_TARGET}
    main.cpp
    winservice.cpp
    winservice.h
    pipeserver.cpp
    pipeserver.h
    ipcsession.cpp
    ipcsession.h
    servicelog.cpp
    servicelog.h
)

target_link_libraries(${SERVICE_TARGET}
    oc-ipc
    Qt5::Core
    Qt5::Network
    advapi32
)

set_target_properties(${SERVICE_TARGET} PROPERTIES
    WIN32_EXECUTABLE OFF
)

# Staged into the same install dir as the GUI, so it shares the GUI's
# fixup_bundle'd Qt5Core/Qt5Network DLLs. Proper service deployment +
# registration is wired in P6 (NSIS).
install(TARGETS ${SERVICE_TARGET}
    RUNTIME DESTINATION . COMPONENT Service
)
