
function(build_component name src)
    add_library(obj-${name} OBJECT ${src})
    set_property(TARGET obj-${name} PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(obj-${name} PRIVATE RTAPI USPACE __MODULE__)

    add_custom_command(OUTPUT ${name}.tmp
            COMMAND ld -r -s -o ${name}.tmp $<TARGET_OBJECTS:obj-${name}>
            DEPENDS obj-${name})
    add_custom_command(OUTPUT ${name}.ver
            COMMAND sh ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/generate_version_script.sh ${name}.tmp > ${name}.ver
            DEPENDS ${name}.tmp scripts)

    add_custom_target(${name}-ver ALL DEPENDS ${name}.ver)

    add_library(${name} SHARED $<TARGET_OBJECTS:obj-${name}>)
    target_link_libraries(${name} PRIVATE rtapi_lib)
    add_dependencies(${name} ${name}-ver)
    
    set_target_properties(${name} PROPERTIES PREFIX "")
    set_target_properties(${name} PROPERTIES LINK_FLAGS "-Bsymbolic -Wl,--version-script='${name}.ver'")
    set_target_properties(${name} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/rtlib)
endfunction()

function(compile_component name src)
    get_filename_component(SRC_NAME ${src} NAME_WE)
    add_custom_command(OUTPUT ${SRC_NAME}.c
            COMMAND halcompile -o ${SRC_NAME}.c ${src}
            DEPENDS halcompile)

    build_component(${name} ${SRC_NAME})
endfunction()

build_component(hal_rt ../hal_lib.c)
build_component(boss_plc boss_plc.c)
build_component(debounce debounce.c)
build_component(encoder encoder.c)
build_component(counter counter.c)
build_component(encoder_ratio encoder_ratio.c)
build_component(stepgen stepgen.c)
build_component(lcd lcd.c)
build_component(matrix_kb matrix_kb.c)
build_component(mux_generic mux_generic.c)
build_component(pwmgen pwmgen.c)
build_component(siggen siggen.c)
build_component(pid pid.c)
build_component(at_pid at_pid.c)
build_component(threads threads.c)
build_component(supply supply.c)
build_component(sim_encoder sim_encoder.c)
build_component(weighted_sum weighted_sum.c)
build_component(watchdog watchdog.c)
build_component(modmath modmath.c)
build_component(streamer streamer.c)
build_component(sampler sampler.c)

set(CLASSICLADDER_RT_SOURCES
        ../classicladder/module_hal.c
        ../classicladder/arithm_eval.c
        ../classicladder/arrays.c
        ../classicladder/calc.c
        ../classicladder/calc_sequential.c
        ../classicladder/manager.c
        ../classicladder/symbols.c
        ../classicladder/vars_access.c
        )

build_component(classicladder_rt ${CLASSICLADDER_RT_SOURCES})

if(${BUILD_PARPORT})
    build_component(hal_parport ../drivers/hal_parport.c)
endif()

# drivers
if(${CMAKE_SYSTEM} MATCHES "Linux")
build_component(pci_8255 ../drivers/pci_8255.c)
build_component(hal_tiro ../drivers/hal_tiro.c)
build_component(hal_stg ../drivers/hal_stg.c)
build_component(hal_vti ../drivers/hal_vti.c)
build_component(hal_evoreg ../drivers/hal_evoreg.c)
build_component(hal_motenc ../drivers/hal_motenc.c)
build_component(hal_ax5214h ../drivers/hal_ax5214h.c)
build_component(hal_speaker ../drivers/hal_speaker.c)
build_component(hal_skeleton ../drivers/hal_skeleton.c)
build_component(opto_ac5 ../drivers/opto_ac5.c)
endif()