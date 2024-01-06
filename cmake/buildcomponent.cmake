
function(build_component)
    cmake_parse_arguments(PARSE_ARGV 0 "BUILD_COMPONENT" "" "NAME" "SOURCES;LIBS")

    set(name ${BUILD_COMPONENT_NAME})
    add_library(obj-${name} OBJECT ${BUILD_COMPONENT_SOURCES})
    if(DEFINED BUILD_COMPONENT_LIBS)
        target_link_libraries(obj-${name} PRIVATE ${BUILD_COMPONENT_LIBS})
    endif()
    set_property(TARGET obj-${name} PROPERTY POSITION_INDEPENDENT_CODE ON)
    #    target_compile_options(obj-${name} PRIVATE -rdynamic)
    target_compile_definitions(obj-${name} PRIVATE RTAPI USPACE __MODULE__)

    # binary stripping
    if(NOT DEBUG)
        set(TLINKER -r -s)
    else()
        set(TLINKER -r)
    endif()

    add_custom_command(OUTPUT ${name}.tmp
            COMMAND ld ${TLINKER} -o ${name}.tmp `echo \"$<TARGET_OBJECTS:obj-${name}>\" | sed 's/\;/ /g'`
            DEPENDS obj-${name})
    if(${MAIN_BUILD})
    add_custom_command(OUTPUT ${name}.ver
            COMMAND sh ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/generate_version_script.sh ${name}.tmp > ${name}.ver
            DEPENDS ${name}.tmp scripts)
    else()
    add_custom_command(OUTPUT ${name}.ver
            COMMAND generate_version_script.sh ${name}.tmp > ${name}.ver
            DEPENDS ${name}.tmp)
    endif()

    add_custom_target(${name}-ver ALL DEPENDS ${name}.ver)

    add_library(${name} SHARED $<TARGET_OBJECTS:obj-${name}>)
    target_compile_options(${name} PRIVATE -nostdinc)
    target_link_options(${name} PRIVATE "LINKER:-Bsymbolic,-rpath,${CMAKE_LIBRARY_OUTPUT_DIRECTORY},--version-script=${CMAKE_CURRENT_BINARY_DIR}/${name}.ver")
    add_dependencies(${name} ${name}-ver)

    set_target_properties(${name} PROPERTIES PREFIX "")
    set_target_properties(${name} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${RTLIB_DIR})
    set_property(TARGET ${name} PROPERTY POSITION_INDEPENDENT_CODE ON)
endfunction()

function(build_component_user)
    cmake_parse_arguments(PARSE_ARGV 0 "BUILD_COMPONENT_USER" "" "NAME" "SOURCES")

    # binary stripping
    if(NOT DEBUG)
        set(TLINKER -r -s)
    else()
        set(TLINKER -r)
    endif()

    set(name ${BUILD_COMPONENT_USER_NAME})
    add_executable(${name} ${BUILD_COMPONENT_USER_SOURCES})
    target_link_libraries(${name} PRIVATE hal inifile)
    set_property(TARGET ${name} PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(${name} PRIVATE ULAPI USPACE)
    #target_link_options(${name} PRIVATE "LINKER:-Bsymbolic,-rpath,${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
    set_target_properties(${name} PROPERTIES PREFIX "")
    #todo: variable name not clear / add extra parameter
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${RTLIB_DIR})
    set_property(TARGET ${name} PROPERTY POSITION_INDEPENDENT_CODE ON)
endfunction()

function(compile_component name src userspace relative)

    if(${relative})
        set(S ${CMAKE_CURRENT_SOURCE_DIR}/${src})
    else()
        set(S ${src})
    endif()
    if(${userspace})
        set(SECTION "1")
        set(USERSPACE "--userspace")
    else()
        set(SECTION "9")
        set(USERSPACE "")
    endif()
    get_filename_component(SRC_NAME ${src} NAME_WE)
    # TODO: We might need to update this path, depending on how we build the project.
    set(MAN_PAGE_DIRECTORY ${CMAKE_BINARY_DIR}/docs/man/man${SECTION})
    set(MAN_PAGE ${SRC_NAME}.${SECTION})
    if(${MAIN_BUILD})
        add_custom_command(OUTPUT ${SRC_NAME}.c ${MAN_PAGE}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/halcompile ${USERSPACE} --preprocess -o ${SRC_NAME}.c ${S}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${MAN_PAGE_DIRECTORY}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/halcompile ${USERSPACE} --document -o ${MAN_PAGE_DIRECTORY}/${MAN_PAGE} ${S}
                DEPENDS halcompile_script ${src})
    else()
        add_custom_command(OUTPUT ${SRC_NAME}.c ${MAN_PAGE}
                COMMAND ${Python3_EXECUTABLE} halcompile ${USERSPACE} --preprocess -o ${SRC_NAME}.c ${S}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${MAN_PAGE_DIRECTORY}
                COMMAND ${Python3_EXECUTABLE} halcompile ${USERSPACE} --document -o ${MAN_PAGE_DIRECTORY}/${MAN_PAGE} ${S}
                DEPENDS ${src})
    endif()

    build_component(NAME ${name} SOURCES ${SRC_NAME} LIBS ulapi hal)
endfunction()

function(generate_conv_component name typ1 typ2 foo bar baz)
    if(${MAIN_BUILD})
    add_custom_command(OUTPUT ${name}.comp
            COMMAND sh ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mkconv.sh ${typ1} ${typ2} ${foo} ${bar} ${baz} < ${CMAKE_CURRENT_SOURCE_DIR}/conv.comp.in > ${name}.comp
            DEPENDS scripts conv.comp.in)

    compile_component(${name} ${CMAKE_CURRENT_BINARY_DIR}/${name}.comp ON OFF)
    else()
    add_custom_command(OUTPUT ${name}.comp
            COMMAND sh ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mkconv.sh ${typ1} ${typ2} ${foo} ${bar} ${baz} < ${CMAKE_CURRENT_SOURCE_DIR}/conv.comp.in > ${name}.comp
            DEPENDS conv.comp.in)
    endif()
endfunction()
