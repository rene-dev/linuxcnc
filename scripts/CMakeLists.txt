add_custom_command(
        OUTPUT halrun
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/halrun
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/halrun
)

add_custom_command(
        OUTPUT generate_version_script
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/generate_version_script.sh
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/generate_version_script.sh
)

add_custom_command(
        OUTPUT mkconv
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/components/mkconv.sh
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mkconv.sh
)

add_custom_target(scripts ALL DEPENDS halrun generate_version_script mkconv)