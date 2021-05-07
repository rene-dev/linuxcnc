set(CMAKE_INCLUDE_CURRENT_DIR True)

set(LIBHAL_SOURCES
        hal_lib.c
        )

add_library(hal SHARED ${LIBHAL_SOURCES})
target_include_directories(hal PUBLIC Python3::Python)
target_link_libraries(hal PRIVATE Python3::Python)

if(USE_USPACE)
target_link_libraries(hal PRIVATE Threads::Threads)
endif(USE_USPACE)
target_link_libraries(hal PRIVATE rt)

if(USE_PREEMPT_RT)

endif(USE_PREEMPT_RT)

set_property(TARGET hal PROPERTY POSITION_INDEPENDENT_CODE ON)


set(LIBHALMODULE_SOURCES
        halmodule.cc
        )

add_library(halmodule SHARED ${LIBHALMODULE_SOURCES})
target_include_directories(halmodule PUBLIC Python3::Python)
target_link_libraries(halmodule PRIVATE Threads::Threads)
target_link_libraries(halmodule PRIVATE Python3::Python)
