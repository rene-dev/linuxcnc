include(buildcomponent)

if(LIBMODBUS_FOUND)
    compile_component(pi500_vfd pi500_vfd.comp ON ON)
endif()
