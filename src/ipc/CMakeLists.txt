# oc-ipc — shared privilege-separation IPC protocol (protocol v1).
# QtCore only; linked into both the GUI (client) and the service (server).
# No Q_OBJECT here, so AUTOMOC/UIC/RCC are disabled for a lean static lib.

add_library(oc-ipc STATIC
    protocol.cpp
    protocol.h
    profile.cpp
    profile.h
)

set_target_properties(oc-ipc PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# PUBLIC so dependents get <ipc/...> via the parent src/ dir and "protocol.h"
# locally. CMAKE_INCLUDE_CURRENT_DIR (ProjectSettings) already exposes src/.
target_include_directories(oc-ipc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(oc-ipc
    PUBLIC
        Qt5::Core
)
