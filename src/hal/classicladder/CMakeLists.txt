if(USE_GTK2)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CLASSICLADDER_SOURCES
        arithm_eval.c
        arrays.c
        calc.c
        calc_sequential.c
        classicladder.c
        classicladder_gtk.c
        config.c
        config_gtk.c
        drawing.c
        drawing_sequential.c
        edit.c
        edit_gtk.c
        edit_sequential.c
        editproperties_gtk.c
        emc_mods.c
        files.c
        files_project.c
        files_sequential.c
        manager.c
        manager_gtk.c
        protocol_modbus_master.c
        protocol_modbus_slave.c
        serial_linux.c
        socket_modbus_master.c
        socket_server.c
        spy_vars_gtk.c
        symbols.c
        symbols_gtk.c
        vars_names.c
        vars_access.c
        ../utils/miscgtk.c
        )

add_executable(classicladder ${CLASSICLADDER_SOURCES})
target_include_directories(classicladder PRIVATE ${GTK2_INCLUDE_DIRS})
target_include_directories(classicladder PRIVATE ${Intl_INCLUDE_DIRS})
target_link_libraries(classicladder PRIVATE hal)
target_link_libraries(classicladder PRIVATE Threads::Threads)
target_link_libraries(classicladder PRIVATE ${GTK2_LIBRARIES})
target_link_libraries(classicladder PRIVATE ${Intl_LIBRARIES})
target_compile_definitions(classicladder PRIVATE GTK_INTERFACE GTK2)
get_uspace_config(classicladder)

endif(USE_GTK2)
