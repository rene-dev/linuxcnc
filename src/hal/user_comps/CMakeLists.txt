include(buildcomponent)
include(pythonBinary)

if(LIBMODBUS_FOUND)
    add_executable(gs2_vfd gs2_vfd.c)
    target_compile_options(gs2_vfd
            PRIVATE
            ${LIBMODBUS_CFLAGS}
    )
    target_include_directories(gs2_vfd
            PRIVATE
            ${LIBMODBUS_INCLUDE_DIRS}
    )
    target_link_libraries(gs2_vfd
            PRIVATE
            hal
            ${LIBMODBUS_LIBRARIES}
    )
    get_uspace_config(gs2_vfd)

    add_executable(hy_gt_vfd hy_gt_vfd.c)
    target_compile_options(hy_gt_vfd
            PRIVATE
            ${LIBMODBUS_CFLAGS}
    )
    target_include_directories(hy_gt_vfd
            PRIVATE
            ${LIBMODBUS_INCLUDE_DIRS}
    )
    target_link_libraries(hy_gt_vfd
            PRIVATE
            hal
            ${LIBMODBUS_LIBRARIES}
    )
    get_uspace_config(hy_gt_vfd)

    add_executable(svd-ps_vfd svd-ps_vfd.c)
    target_compile_options(svd-ps_vfd
            PRIVATE
            ${LIBMODBUS_CFLAGS}
    )
    target_include_directories(svd-ps_vfd
            PRIVATE
            ${LIBMODBUS_INCLUDE_DIRS}
    )
    target_link_libraries(svd-ps_vfd
            PRIVATE
            hal
            ${LIBMODBUS_LIBRARIES}
    )
    get_uspace_config(svd-ps_vfd)
endif()

if(LIBUSB10_FOUND)
    add_executable(xhc-hb04 xhc-hb04.cc)
    target_compile_options(xhc-hb04
            PRIVATE
            ${LIBUSB10_CFLAGS}
    )
    target_include_directories(xhc-hb04
            PRIVATE
            ${LIBUSB10_INCLUDE_DIRS}
    )
    target_link_libraries(xhc-hb04
            PRIVATE
            libnml_inifile
            hal
            ${LIBUSB10_LIBRARIES}
    )
    get_uspace_config(xhc-hb04)
endif()

add_executable(sendkeys sendkeys.c)
target_include_directories(sendkeys
        PRIVATE
        ${LIBUSB10_INCLUDE_DIRS}
)
target_link_libraries(sendkeys
        PRIVATE
        hal
)
get_uspace_config(sendkeys)


python_binary(gladevcp.py)
python_binary(hal_input.py)
python_binary(mitsub_vfd.py)
python_binary(mqtt-publisher.py)
python_binary(pmx485.py)
python_binary(pyvcp.py)
python_binary(scorbot-er-3.py)
python_binary(sim-torch.py)
python_binary(z_level_compensation.py)

compile_component(thermistor thermistor.comp ON ON)

add_subdirectory(huanyang-vfd)
add_subdirectory(mb2hal)
# FIXME: Needs ModBus
#add_subdirectory(pi500_vfd)
add_subdirectory(vfdb_vfd)
add_subdirectory(vfs11_vfd)
add_subdirectory(vismach)
# FIXME: Needs ModBus
#add_subdirectory(wj200_vfd)
add_subdirectory(xhc-whb04b-6)
