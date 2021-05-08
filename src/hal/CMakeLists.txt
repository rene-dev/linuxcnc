set(CMAKE_INCLUDE_CURRENT_DIR True)

set(LIBHAL_SOURCES
        hal_lib.c
        )

add_library(hal SHARED ${LIBHAL_SOURCES})

if(USE_USPACE)
    target_link_libraries(hal PRIVATE Threads::Threads)
    target_compile_definitions(hal PRIVATE ULAPI)
else(USE_USPACE)
    target_compile_definitions(hal PRIVATE RTAPI)
endif(USE_USPACE)
target_link_libraries(hal PRIVATE rt)

set_property(TARGET hal PROPERTY POSITION_INDEPENDENT_CODE ON)


set(LIBHALMODULE_SOURCES
        halmodule.cc
        )

add_library(halmodule SHARED ${LIBHALMODULE_SOURCES})
target_include_directories(halmodule PUBLIC Python3::Python)
target_link_libraries(halmodule PRIVATE Threads::Threads)
target_link_libraries(halmodule PRIVATE Python3::Python)

if(USE_USPACE)
    target_link_libraries(halmodule PRIVATE Threads::Threads)
    target_compile_definitions(halmodule PRIVATE ULAPI)
else(USE_USPACE)
    target_compile_definitions(halmodule PRIVATE RTAPI)
endif(USE_USPACE)
target_link_libraries(halmodule PRIVATE rt)

set(HALCMD_SOURCES
        utils/halcmd.c
        utils/halcmd_commands.c
        utils/halcmd_main.c
        )
add_executable(halcmd ${HALCMD_SOURCES})
target_include_directories(halcmd PRIVATE ${TCL_INCLUDE})
target_link_libraries(halcmd PRIVATE ${TCL_LIBRARIES})

if(USE_USPACE)
    target_link_libraries(halcmd PRIVATE Threads::Threads)
    target_compile_definitions(halcmd PRIVATE ULAPI)
else(USE_USPACE)
    target_compile_definitions(halcmd PRIVATE RTAPI)
endif(USE_USPACE)

set(HALSH_SOURCES
        utils/halcmd.c
        utils/halcmd_commands.c
        utils/halsh.c
        )
add_library(halsh SHARED ${HALSH_SOURCES})
target_include_directories(halsh PRIVATE ${READLINE_INCLUDE})
target_link_libraries(halsh PRIVATE ${READLINE_LIBRARIES})

if(USE_USPACE)
    target_link_libraries(halsh PRIVATE Threads::Threads)
    target_compile_definitions(halsh PRIVATE ULAPI)
else(USE_USPACE)
    target_compile_definitions(halsh PRIVATE RTAPI)
endif(USE_USPACE)
