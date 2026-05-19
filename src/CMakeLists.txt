
set_property(SOURCE ${PROJECT_NAME}.cpp PROPERTY SKIP_AUTOGEN ON)
set_property(SOURCE timestamp.cpp PROPERTY SKIP_AUTOGEN ON)

# Keenetic camouflage fork: trimmed source list.
# Removed (vs upstream openconnect-gui 1.5.3):
#   cert.cpp, key.cpp, keypair.cpp           — per-user certificate auth UI
#   gtdb.cpp                                  — TOFU cert pinning (replaced by bundled CA)
#   cryptdata.cpp                             — settings-blob encryption (we use QSettings + QCryptographicHash + Windows DPAPI directly)
#   dialog/MyCertMsgBox.{cpp,h,ui}            — first-time cert TOFU dialog
set(${PROJECT_NAME}_SRCS
    ${PROJECT_NAME}.cpp
    ${PROJECT_NAME}.qrc
    config.h.in
    dialog/MyInputDialog.cpp
    dialog/MyMsgBox.cpp
    dialog/editdialog.cpp
    dialog/editdialog.ui
    dialog/NewProfileDialog.cpp
    dialog/NewProfileDialog.ui
    dialog/logdialog.cpp
    dialog/logdialog.ui
    dialog/VpnProtocolModel.cpp
    dialog/mainwindow.cpp
    dialog/mainwindow.ui
    main.cpp
    server_storage.cpp

    timestamp.cpp

    vpninfo.cpp

    logger.cpp

    FileLogger.cpp
)

set_source_files_properties(
    ${PROJECT_NAME}.cpp
    timestamp.cpp
    PROPERTIES
        GENERATED true
)

configure_file(
    config.h.in
    config.h
    @ONLY
)

if(MINGW)
    list(APPEND ${PROJECT_NAME}_RESOURCES
        ${PROJECT_NAME}.rc.in
        ${PROJECT_NAME}.rc
    )

    set_source_files_properties(
       ${PROJECT_NAME}.rc
        PROPERTIES
            GENERATED true
    )
endif()
# macOS support removed in keenetic-camouflage fork (Windows-only).

add_executable(${PROJECT_NAME}
    ${${PROJECT_NAME}_SRCS}
    ${${PROJECT_NAME}_RESOURCES}
)
if(MINGW)
    add_definitions(-DSPDLOG_WCHAR_FILENAMES)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/external/vpnc-script.js $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Deploying vpnc script..."
        VERBATIM
    )

    add_dependencies(${PROJECT_NAME}
        openconnect-keenetic
        vpnc-scripts-${vpnc-scripts-TAG}
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE true
    )

    target_link_libraries(${PROJECT_NAME}
        # system libraries
        wsock32
        ws2_32
        iphlpapi

        # 3rd party dependencies (bundled DLLs from external/openconnect-keenetic_mingw64.zip)
        openconnect::gmp
        openconnect::gnutls
        openconnect::hogweed
        openconnect::nettle
        openconnect::openconnect
        openconnect::p11-kit
        openconnect::xml2
    )
endif()


# common stuff to link...
target_link_libraries (${PROJECT_NAME}
    spdlog::spdlog
    qt-solutions::qtsingleapplication

    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Network
)

if(UNIX)
    target_link_libraries(${PROJECT_NAME}
        ${OPENCONNECT_LIBRARIES}
        ${GNUTLS_LIBRARY}

        Threads::Threads
    )
endif()

# install executable
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION . COMPONENT App
    LIBRARY DESTINATION . COMPONENT App
)

# fixup the bundle
get_target_property(QT_LIBRARY_DIR Qt5::Core LOCATION)
get_filename_component(QT_LIBRARY_DIR ${QT_LIBRARY_DIR} PATH)
get_filename_component(QT_LIBRARY_DIR "${QT_LIBRARY_DIR}/.." ABSOLUTE)
list(APPEND libSearchDirs ${QT_LIBRARY_DIR})

if(MINGW)
    set(APPS "\${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.exe")
    list(APPEND libSearchDirs ${CMAKE_BINARY_DIR}/external/lib)
    list(APPEND libSearchDirs ${CMAKE_BINARY_DIR}/external/bin)

    # Qt Platform Plugin
    install(FILES
        ${_qwindows_dll}
        DESTINATION plugins/platforms
        COMPONENT App
    )
    # qt.conf was in nsis/, now generated inline.
    install(CODE "file(WRITE \"\${CMAKE_INSTALL_PREFIX}/qt.conf\" \"[Paths]\\nPlugins=plugins\\n\")" COMPONENT App)

    set(additionalLib ${_qwindows_dll})
    if(NOT Qt5Core_VERSION STRLESS "5.10")
        install(FILES
            ${_qwinstyle_dylib}
            DESTINATION plugins/styles
            COMPONENT App
        )
    endif()
else()
    message(STATUS "Non-MinGW host — skip bundling (this fork is Windows-only)")
endif()

install(CODE "
    include(BundleUtilities)
    fixup_bundle(\"${APPS}\" \"${additionalLib}\" \"${libSearchDirs}\")
    " DESTINATION . COMPONENT App
)
