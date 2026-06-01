# CPack pre-build hook: code-sign the staged executables AFTER install/
# fixup_bundle but BEFORE the MSI is assembled, so the signed binaries are the
# ones embedded in the package. No-op unless OCG_SIGN_THUMB is set in the env.
#
# Needed because fixup_bundle (BundleUtilities) rewrites the GUI exe and strips
# any prior signature; signing the source tree therefore doesn't survive.

if(NOT DEFINED ENV{OCG_SIGN_THUMB})
    return()
endif()

set(_thumb "$ENV{OCG_SIGN_THUMB}")
set(_st "$ENV{OCG_SIGNTOOL}")
set(_ts "$ENV{OCG_SIGN_TS}")

set(_roots "${CPACK_TEMPORARY_INSTALL_DIRECTORY}" "${CPACK_TEMPORARY_DIRECTORY}")
set(_targets openconnect-gui.exe openconnect-gui-service.exe openconnect.exe)

foreach(_root ${_roots})
    if(NOT _root OR NOT EXISTS "${_root}")
        continue()
    endif()
    file(GLOB_RECURSE _exes "${_root}/*.exe")
    foreach(_f ${_exes})
        get_filename_component(_n "${_f}" NAME)
        if(_n IN_LIST _targets)
            execute_process(
                COMMAND "${_st}" sign /sha1 "${_thumb}" /fd sha256 /tr "${_ts}" /td sha256 "${_f}"
                RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
            message(STATUS "[sign-staged] ${_n} -> rc=${_rc}")
        endif()
    endforeach()
endforeach()
