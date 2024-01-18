include(buildcomponent)

if(LIBMODBUS_FOUND)
    compile_component(wj200_vfd wj200_vfd.comp ON ON)
endif()
