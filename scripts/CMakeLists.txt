add_custom_command(
        OUTPUT halrun
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/halrun
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/halrun
        DEPENDS halrun)


add_custom_target(scripts ALL DEPENDS halrun)