if(LIBMODBUS_FOUND)
    add_executable(vfs11_vfd vfs11_vfd.c)
    target_include_directories(vfs11_vfd
            PRIVATE
            ${LIBMODBUS_INCLUDE_DIRS}
    )
    target_link_libraries(vfs11_vfd
            PRIVATE
            hal
            libnml_inifile
            ${LIBMODBUS_LIBRARIES}
    )
    get_uspace_config(vfs11_vfd)
endif()
