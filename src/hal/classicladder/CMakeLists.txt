set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CLASSICLADDER_SOURCES
        arithm_eval.c
        arrays.c
        calc.c
        calc_sequential.c
        classicladder.c
        classicladder_gtk.c
        config_gtk.c
        drawing.c
        drawing_sequential.c
        edit.c
        edit_copy.c
        edit_gtk.c
        edit_sequential.c
        editproperties_gtk.c
        emc_mods.c
        files.c
        files_project.c
        files_sequential.c
        manager.c
        manager_gtk.c
        menu_and_toolbar_gtk.c
        print_gtk.c
        protocol_modbus_master.c
        protocol_modbus_slave.c
        serial_linux.c
        socket_modbus_master.c
        socket_server.c
        spy_vars_gtk.c
        symbols.c
        symbols_gtk.c
        vars_access.c
        vars_names.c
)

add_compile_definitions(
        DYNAMIC_PLCSIZE
        HAL_SUPPORT
        MODBUS_IO_MASTER
        OLD_TIMERS_MONOS_SUPPORT
        RT_SUPPORT
        SEQUENTIAL_SUPPORT
)

add_executable(classicladder ${CLASSICLADDER_SOURCES})
target_include_directories(classicladder
        PRIVATE
        ${GTK3_INCLUDE_DIRS}
        ${Intl_INCLUDE_DIRS}
)
target_link_libraries(classicladder
        PRIVATE
        hal
        Threads::Threads
        ${GTK3_LIBRARIES}
        ${Intl_LIBRARIES}
)
target_compile_definitions(classicladder
        PRIVATE
        GTK_INTERFACE
)
target_compile_options(classicladder
        PRIVATE
        -Wno-deprecated-declarations
)
get_uspace_config(classicladder)

set(CLASSICLADDER_RT_SOURCES
        arithm_eval.c
        arrays.c
        calc.c
        calc_sequential.c
        manager.c
        module_hal.c
        symbols.c
        vars_access.c
)
add_library(classicladder_rt SHARED ${CLASSICLADDER_RT_SOURCES})
target_link_libraries(classicladder_rt
        PRIVATE
        hal
        ulapi
        Threads::Threads
)
set_target_properties(classicladder_rt
        PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${RTLIB_DIR}
)
target_compile_definitions(classicladder_rt
        PRIVATE
        MODULE
        RTAPI
)
