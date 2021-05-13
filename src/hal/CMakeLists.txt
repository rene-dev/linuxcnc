set(CMAKE_INCLUDE_CURRENT_DIR True)

set(LIBHAL_SOURCES
        hal_lib.c
        )

add_library(hal SHARED ${LIBHAL_SOURCES})
set_property(TARGET hal PROPERTY POSITION_INDEPENDENT_CODE ON)
get_uspace_config(hal)
target_link_libraries(hal PRIVATE ulapi)

set_property(TARGET hal PROPERTY POSITION_INDEPENDENT_CODE ON)


set(LIBHALMODULE_SOURCES
        halmodule.cc
        )

add_library(halmodule SHARED ${LIBHALMODULE_SOURCES})
target_include_directories(halmodule PUBLIC Python3::Python)
target_link_libraries(halmodule PRIVATE Threads::Threads)
target_link_libraries(halmodule PRIVATE Python3::Python)
target_link_libraries(halmodule PRIVATE rt)
get_uspace_config(halmodule)

set(HALCMD_SOURCES
        utils/halcmd.c
        utils/halcmd_commands.c
        utils/halcmd_main.c
        utils/halcmd_completion.c
        )
add_executable(halcmd ${HALCMD_SOURCES})
target_include_directories(halcmd PRIVATE ${TCL_INCLUDE})
target_include_directories(halcmd PRIVATE ${READLINE_INCLUDE})
target_link_libraries(halcmd PRIVATE ${TCL_LIBRARIES})
target_link_libraries(halcmd PRIVATE ${READLINE_LIBRARIES})
target_link_libraries(halcmd PRIVATE inifile)
target_link_libraries(halcmd PRIVATE hal)
target_link_libraries(halcmd PRIVATE rt)
get_uspace_config(halcmd)

set(HALSH_SOURCES
        utils/halcmd.c
        utils/halcmd_commands.c
        utils/halsh.c
        )
add_library(halsh SHARED ${HALSH_SOURCES})
target_include_directories(halsh PRIVATE ${READLINE_INCLUDE})
target_include_directories(halsh PRIVATE ${TCL_INCLUDE_PATH})
target_link_libraries(halsh PRIVATE ${READLINE_LIBRARIES})
target_link_libraries(halsh PRIVATE ${TCL_LIBRARY})
get_uspace_config(halsh)

add_executable(halstreamer components/streamer_usr.c)
target_link_libraries(halstreamer PRIVATE hal)
get_uspace_config(halstreamer)

add_executable(halsampler components/sampler_usr.c)
target_link_libraries(halsampler PRIVATE hal)
get_uspace_config(halsampler)

add_executable(halrmt utils/halrmt.c)
target_link_libraries(halrmt PRIVATE hal)
get_uspace_config(halrmt)

if(USE_GTK2)

set(HALMETER_SOURCES
        utils/meter.c
        utils/miscgtk.c
        )
add_executable(halmeter ${HALMETER_SOURCES})
target_include_directories(halmeter PRIVATE ${GTK2_INCLUDE_DIRS})
target_include_directories(halmeter PRIVATE ${Intl_INCLUDE_DIRS})
target_link_libraries(halmeter PRIVATE hal)
target_link_libraries(halmeter PRIVATE ${GTK2_LIBRARIES})
target_link_libraries(halmeter PRIVATE ${Intl_LIBRARIES})
get_uspace_config(halmeter)

set(HALSCOPE_SOURCES
        utils/scope.c
        utils/scope_horiz.c
        utils/scope_vert.c
        utils/scope_trig.c
        utils/scope_disp.c
        utils/scope_files.c
        utils/miscgtk.c
        )
add_executable(halscope ${HALSCOPE_SOURCES})
target_include_directories(halscope PRIVATE ${GTK2_INCLUDE_DIRS})
target_include_directories(halscope PRIVATE ${Intl_INCLUDE_DIRS})
target_link_libraries(halscope PRIVATE hal)
target_link_libraries(halscope PRIVATE m)
target_link_libraries(halscope PRIVATE ${GTK2_LIBRARIES})
target_link_libraries(halscope PRIVATE ${Intl_LIBRARIES})
get_uspace_config(halscope)

if(USE_RTAI)
    set(PCIWRITE_SOURCES
            utils/pci_write.c
            utils/upci.c
            )

    add_executable(pci_write ${PCIWRITE_SOURCES})

    set(PCIREAD_SOURCES
            utils/pci_read.c
            utils/upci.c
            )

    add_executable(pci_read ${PCIREAD_SOURCES})
endif()

endif(USE_GTK2)

add_custom_command(OUTPUT halcompile.py
        COMMAND yapps2 ${CMAKE_CURRENT_SOURCE_DIR}/utils/halcompile.g ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/halcompile.py
        DEPENDS utils/halcompile.g)
add_custom_target(halcompile ALL DEPENDS halcompile.py)