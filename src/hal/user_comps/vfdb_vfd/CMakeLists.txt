if(LIBMODBUS_FOUND)
    add_executable(vfdb_vfd vfdb_vfd.c)
    target_include_directories(vfdb_vfd
            PRIVATE
            ${LIBMODBUS_INCLUDE_DIRS}
    )
    target_link_libraries(vfdb_vfd
            PRIVATE
            hal
            libnml_inifile
            m
            ${LIBMODBUS_LIBRARIES}
    )
    get_uspace_config(vfdb_vfd)
endif()
