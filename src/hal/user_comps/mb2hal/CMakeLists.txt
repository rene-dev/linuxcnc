if(LIBMODBUS_FOUND)
    set(MB2HAL_SOURCES
            mb2hal.c
            mb2hal_init.c
            mb2hal_hal.c
            mb2hal_modbus.c
    )

    add_executable(mb2hal ${MB2HAL_SOURCES})
    target_include_directories(mb2hal
            PRIVATE
            ${LIBMODBUS_INCLUDE_DIRS}
    )
    target_link_libraries(mb2hal
            PRIVATE
            hal
            libnml_inifile
            Threads::Threads
            ${LIBMODBUS_LIBRARIES}
    )
    get_uspace_config(mb2hal)
endif()
