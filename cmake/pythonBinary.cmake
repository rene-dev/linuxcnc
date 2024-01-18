function(python_binary src)
    get_filename_component(SRC_NAME ${src} NAME_WE)
    set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/${src})
    set(OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${SRC_NAME})

    add_custom_target(${SRC_NAME} DEPENDS ${OUTPUT})
    add_custom_command(
            OUTPUT ${OUTPUT}
            COMMAND ${Python3_EXECUTABLE} -m py_compile ${SRC}
            COMMENT "Syntax checking Python script ${src}"
            COMMAND ${CMAKE_COMMAND} -E copy ${SRC} ${OUTPUT}
            COMMAND chmod 755 ${OUTPUT}
            DEPENDS ${SRC} ${Python3_EXECUTABLE}
    )
endfunction()
