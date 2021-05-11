set(CMAKE_INCLUDE_CURRENT_DIR True)

add_library(mathstubs mathstubs.c)

# use this function for everything that needs rtapi
function(get_uspace_config tgt)
    if(USE_USPACE)
        target_link_libraries(${tgt} PRIVATE Threads::Threads)
        target_compile_definitions(${tgt} PRIVATE ULAPI)
    else(USE_USPACE)
        target_compile_definitions(${tgt} PRIVATE RTAPI)
    endif(USE_USPACE)
endfunction()

if(USE_RTAI)
    find_package(RTAI REQUIRED)
    set(RTAI_SOURCES rtapi/uspace_rtai.cc)
    add_library(uspace-rtai SHARED ${RTAI_SOURCES})
    add_library(ulapi rtai_ulapi.c)
    get_uspace_config(ulapi)
    set_property(TARGET ulapi PROPERTY POSITION_INDEPENDENT_CODE ON)
endif(USE_RTAI)

if(USE_PREEMPT_RT)
    set(RTAPI_SOURCES
            uspace_rtapi_app.cc
            uspace_rtapi_string.c
            rtapi_pci.cc
            )

    set_source_files_properties(rtapi_pci.cc PROPERTIES COMPILE_FLAGS -O0)
    
    add_library(rtapi_lib ${RTAPI_SOURCES})
    target_link_libraries(rtapi_lib Threads::Threads)
    target_link_libraries(rtapi_lib ${CMAKE_DL_LIBS})
    target_compile_options(rtapi_lib PRIVATE -rdynamic)
    target_compile_definitions(rtapi_lib PRIVATE SIM RTAPI)
    set_property(TARGET rtapi_lib PROPERTY POSITION_INDEPENDENT_CODE ON)

    add_executable(rtapi_app ${RTAPI_SOURCES})
    target_link_libraries(rtapi_app Threads::Threads)
    target_link_libraries(rtapi_app ${CMAKE_DL_LIBS})
    target_compile_options(rtapi_app PRIVATE -rdynamic)
    target_compile_definitions(rtapi_app PRIVATE SIM RTAPI)
            
    if(BUILD_PARPORT)
	    target_sources(rtapi_app PRIVATE uspace_rtapi_parport.cc)
    endif(BUILD_PARPORT)

    if(BUILD_UDEV)
    else(BUILD_UDEV)
        target_compile_definitions(rtapi_app PRIVATE NO_LIBUDEV)
    endif(BUILD_UDEV)

    add_library(ulapi uspace_ulapi.c)
    get_uspace_config(ulapi)
    set_property(TARGET ulapi PROPERTY POSITION_INDEPENDENT_CODE ON)

    target_compile_definitions(mathstubs PRIVATE SIM RTAPI)
    set_property(TARGET mathstubs PROPERTY POSITION_INDEPENDENT_CODE ON)
endif(USE_PREEMPT_RT)

if(USE_XENOMAI)
    find_package(Xenomai REQUIRED)
    set(XENOMAI_SOURCES
            rtapi/uspace_xenomai.cc
            )
    add_library(uspace-xenomai SHARED ${XENOMAI_SOURCES})
    set_property(TARGET uspace-xenomai PROPERTY POSITION_INDEPENDENT_CODE ON)

    add_library(ulapi rtapi/uspace_ulapi.c)
endif(USE_XENOMAI)
