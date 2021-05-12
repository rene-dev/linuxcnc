

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


#obj-$(CONFIG_BOSS_PLC) += boss_plc.o
#boss_plc-objs := hal/components/boss_plc.o $(MATHSTUB)
#obj-$(CONFIG_DEBOUNCE) += debounce.o
#debounce-objs := hal/components/debounce.o $(MATHSTUB)
#obj-$(CONFIG_ENCODER) += encoder.o
#encoder-objs := hal/components/encoder.o $(MATHSTUB)
#obj-$(CONFIG_COUNTER) += counter.o
#counter-objs := hal/components/counter.o $(MATHSTUB)
#obj-$(CONFIG_ENCODER_RATIO) += encoder_ratio.o
#encoder_ratio-objs := hal/components/encoder_ratio.o $(MATHSTUB)
#obj-$(CONFIG_STEPGEN) += stepgen.o
#stepgen-objs := hal/components/stepgen.o $(MATHSTUB)
#obj-$(CONFIG_LCD) += lcd.o
#lcd-objs := hal/components/lcd.o $(MATHSTUB)
#obj-$(CONFIG_MATRIX_KB) += matrix_kb.o
#matrix_kb-objs := hal/components/matrix_kb.o $(MATHSTUB)
#obj-$(CONFIG_MUX_GENERIC) += mux_generic.o
#mux_generic-objs := hal/components/mux_generic.o $(MATHSTUB)
#obj-$(CONFIG_PWMGEN) += pwmgen.o
#pwmgen-objs := hal/components/pwmgen.o $(MATHSTUB)
#obj-$(CONFIG_SIGGEN) += siggen.o
#siggen-objs := hal/components/siggen.o $(MATHSTUB)
#obj-$(CONFIG_PID) += pid.o
#pid-objs := hal/components/pid.o $(MATHSTUB)
#obj-$(CONFIG_AT_PID) += at_pid.o
#at_pid-objs := hal/components/at_pid.o $(MATHSTUB)
#obj-$(CONFIG_PID) += threads.o
#threads-objs := hal/components/threads.o $(MATHSTUB)
#obj-$(CONFIG_SUPPLY) += supply.o
#supply-objs := hal/components/supply.o $(MATHSTUB)
#obj-$(CONFIG_SIM_ENCODER) += sim_encoder.o
#sim_encoder-objs := hal/components/sim_encoder.o $(MATHSTUB)
#obj-$(CONFIG_WEIGHTED_SUM) += weighted_sum.o
#weighted_sum-objs := hal/components/weighted_sum.o $(MATHSTUB)
#obj-$(CONFIG_WATCHDOG) += watchdog.o
#watchdog-objs := hal/components/watchdog.o $(MATHSTUB)
#obj-$(CONFIG_MODMATH) += modmath.o
#modmath-objs := hal/components/modmath.o $(MATHSTUB)
#obj-$(CONFIG_STREAMER) += streamer.o
#streamer-objs := hal/components/streamer.o $(MATHSTUB)
#obj-$(CONFIG_SAMPLER) += sampler.o
#sampler-objs := hal/components/sampler.o $(MATHSTUB)
