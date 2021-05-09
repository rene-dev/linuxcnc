set(NMLINTF_SOURCES
        ../nml_intf/emcglb.c
        ../rs274ngc/modal_state.cc
        ../nml_intf/emc.cc
        ../nml_intf/emcpose.c
        ../nml_intf/emcargs.cc
        ../nml_intf/emcops.cc
        ../nml_intf/canon_position.cc
        ../ini/emcIniFile.cc
        ../ini/iniaxis.cc
        ../ini/inijoint.cc
        ../ini/initraj.cc
        ../ini/inihal.cc
        ../nml_intf/interpl.cc
        )

add_library(linuxcnc ${NMLINTF_SOURCES})
get_uspace_config(linuxcnc)