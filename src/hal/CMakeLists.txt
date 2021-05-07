set(CMAKE_INCLUDE_CURRENT_DIR True)

set(LIBHAL_SOURCES
        hal_lib.c
        )

set(LIBHALMODULE_SOURCES
        halmodule.cc
        )

add_library(hal SHARED ${LIBHAL_SOURCES})
target_include_directories(hal PUBLIC Python3::Python)
target_link_libraries(hal PUBLIC Python3::Python)
set_property(TARGET hal PROPERTY POSITION_INDEPENDENT_CODE ON)

add_library(halmodule SHARED ${LIBHALMODULE_SOURCES})
target_include_directories(halmodule PUBLIC Python3::Python)
target_link_libraries(halmodule PUBLIC Python3::Python)
