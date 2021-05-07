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
