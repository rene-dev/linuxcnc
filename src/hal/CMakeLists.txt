set(CMAKE_INCLUDE_CURRENT_DIR True)

set(LIBHAL_SOURCES
        hal_lib.c
        )

add_library(hal SHARED ${LIBHAL_SOURCES})

get_uspace_config(hal)
target_link_libraries(hal PRIVATE rt)
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
target_include_directories(halsh PRIVATE ${TCL_INCLUDE})
target_link_libraries(halsh PRIVATE ${READLINE_LIBRARIES})
target_link_libraries(halsh PRIVATE ${TCL_LIBRARIES})
get_uspace_config(halsh)

add_executable(hal_streamer components/streamer_usr.c)
target_link_libraries(hal_streamer PRIVATE hal)
get_uspace_config(hal_streamer)

add_executable(hal_sampler components/sampler_usr.c)
target_link_libraries(hal_sampler PRIVATE hal)
get_uspace_config(hal_sampler)

