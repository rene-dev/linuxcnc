set(CMAKE_INCLUDE_CURRENT_DIR True)

add_library(mathstubs mathstubs.c)

if(USE_RTAI)
    find_package(RTAI REQUIRED)
    set(RTAI_SOURCES rtapi/uspace_rtai.cc)
    add_library(uspace-rtai SHARED ${RTAI_SOURCES})
endif(USE_RTAI)

if(USE_PREEMPT_RT)
    set(RTAPI_SOURCES
            uspace_rtapi_app.cc
            uspace_rtapi_parport.cc
            uspace_rtapi_string.c
            rtapi_pci.cc
            )

    set_source_files_properties(rtapi_pci.cc PROPERTIES COMPILE_FLAGS -O0)

    add_executable(rtapi_app ${RTAPI_SOURCES})
    target_link_libraries(rtapi_app Threads::Threads)
    target_link_libraries(rtapi_app ${CMAKE_DL_LIBS})
    target_compile_options(rtapi_app PRIVATE -rdynamic)
    target_compile_definitions(rtapi_app PRIVATE SIM RTAPI)
    target_compile_definitions(mathstubs PRIVATE SIM RTAPI)

    if(BUILD_UDEV)
    else(BUILD_UDEV)
        target_compile_definitions(rtapi_app PRIVATE NO_LIBUDEV)
    endif(BUILD_UDEV)
endif(USE_PREEMPT_RT)

if(USE_XENOMAI)
    find_package(Xenomai REQUIRED)
    set(XENOMAI_SOURCES
            rtapi/uspace_xenomai.cc
            )
    add_library(uspace-xenomai SHARED ${XENOMAI_SOURCES})
    set_property(TARGET uspace-xenomai PROPERTY POSITION_INDEPENDENT_CODE ON)
endif(USE_XENOMAI)
