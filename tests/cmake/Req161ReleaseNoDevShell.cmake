# REQ-161: shipped GoSurvey.exe must not contain Test Engine / DevShell ASCII markers.
if(NOT DEFINED GOSURVEY_EXE)
  message(FATAL_ERROR "GOSURVEY_EXE is required")
endif()
if(NOT EXISTS "${GOSURVEY_EXE}")
  message(FATAL_ERROR "missing exe: ${GOSURVEY_EXE}")
endif()

file(READ "${GOSURVEY_EXE}" _hex HEX)

function(req161_hex_absent ascii_label hex_needle)
  string(FIND "${_hex}" "${hex_needle}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "REQ-161 leak: '${ascii_label}' found in ${GOSURVEY_EXE}")
  endif()
endfunction()

# ASCII "ImGuiTestEngine"
req161_hex_absent("ImGuiTestEngine" "496d47756954657374456e67696e65")
# ASCII "DevShell_Create"
req161_hex_absent("DevShell_Create" "4465765368656c6c5f437265617465")
