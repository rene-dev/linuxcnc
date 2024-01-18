if(LIBUSB10_FOUND)
    set(XHC_WHB04B6_SRC
            hal.cc
            main.cc
            pendant-types.cc
            pendant.cc
            usb.cc
            xhc-whb04b6.cc
    )

    add_executable(xhc-whb04b-6 ${XHC_WHB04B6_SRC})
    target_include_directories(xhc-whb04b-6
            PRIVATE
            ${LIBUSB10_INCLUDE_DIRS}
    )
    target_link_libraries(xhc-whb04b-6
            PRIVATE
            hal
            ${LIBUSB10_LIBRARIES}
    )
    get_uspace_config(xhc-whb04b-6)
endif()
