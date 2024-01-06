cmake_minimum_required(VERSION 3.23)
project(linuxcnc VERSION 3.0.0)

include(CMakeDependentOption)
include(CMakeDetermineSystem)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

# find external dependecies

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(X11 REQUIRED)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

set(BOOSTPYTHON python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR})

find_package(Boost COMPONENTS ${BOOSTPYTHON} REQUIRED)
find_package(Intl REQUIRED)

find_package(Epoxy REQUIRED)

# for config.h in src/ folder
include_directories(src)

# slightly different locations for TIRPC in linux / bsd
if(${CMAKE_SYSTEM} MATCHES "Linux")
    find_package(TIRPC REQUIRED)
else()
    # assume BSD
    # TODO: replace this with a proper target and move it the the FindTIRPC.cmake
    set(TIRPC_LIBRARIES "")
    set(TIRPC_INCLUDE_DIRS "")
    include_directories(/usr/local/include/)
endif()

find_package(Readline REQUIRED)
find_package(TCL REQUIRED)
find_package(Tclsh REQUIRED)

find_package(GTK3 REQUIRED gtk)

# C and CXX flags
include(buildoptions)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# flag this as main build
# otherwise the build component and compile component functions
# will not add the necessary targets for some scripts like halcompile
set(MAIN_BUILD ON)

# general options
option(RUN_IN_PLACE "activate this if you want to execute linuxcnc in place, instead of installing it." ON)
option(BUILD_UDEV "deactivate this if udev is not available. Was previously named --disable-userspace-pci." OFF)
option(TOOL_NML "Use nml for tool data, default is mmap" OFF)
option(BUILD_PARPORT "whether to build parallel port support or not" ON)

# realtime options
option(USE_PREEMPT_RT "realtime layer preempt_rt used" ON)
option(USE_RTAI "realtime layer rtai used" OFF)
option(USE_XENOMAI "realtime layer xenomai used" OFF)
option(USE_USPACE "whether the realtime layer is in user space or not" ON)
option(USE_KBUILD "whether your kernel uses kbuild or not" OFF)

# check if realtime options sane
if((${USE_PREEMPT_RT} AND (${USE_RTAI} OR ${USE_XENOMAI})) OR (${USE_RTAI} AND ${USE_XENOMAI}))
    message(FATAL_ERROR "invalid realtime options, you may only choose one realtime layer!")
endif()

if(${USE_RTAI} AND ${USE_USPACE})
    message(FATAL_ERROR "RTAI cannot be used in user space!")
endif()

if(NOT ${USE_RTAI} AND ${USE_KBUILD})
    message(FATAL_ERROR "kbuild can only be used with RTAI!")
endif()

if(${USE_RTAI})
    add_compile_definitions(RTAPI_RTAI)
endif()

if(${USE_USPACE})
    add_compile_definitions(RTAPI_USPACE)
endif()

if(${TOOL_NML})
    add_compile_definitions(TOOL_NML)
endif()

add_compile_definitions(
        EMC2_VERSION="3.0.0"
        PACKAGE_VERSION="3.0.0"
        HAVE_READLINE
)

if(NOT ${BUILD_UDEV})
    add_compile_definitions(NO_LIBUDEV)
endif()

# configure variables
# now all defined in activate-linuxcnc.sh
# read from env variables
#if(${RUN_IN_PLACE})
#    set(EMC2_HOME ${CMAKE_BINARY_DIR})
#    add_compile_definitions(EMC2_HOME="${EMC2_HOME}")
#    #add_compile_definitions(EMC2_SUFFIX="(run-in-place)")
#    add_compile_definitions(EMC2_ICON="${EMC2_HOME}/linuxcncicon.png")
#    add_compile_definitions(EMC2_RTLIB_DIR="${EMC2_HOME}/modules")
#    add_compile_definitions(EMC2_BIN_DIR="${EMC2_HOME}/bin")
#    add_compile_definitions(RTDIR="${EMC2_HOME}/modules")
#    #    EMC2_LATENCY_SCRIPT=$EMC2_HOME/scripts/latency-test
#    #EMC2_SCRIPT=$EMC2_HOME/scripts/linuxcnc
#    #EMC2_ICON=$EMC2_HOME/linuxcncicon.png
#    #EMC2_BIN_DIR=$EMC2_HOME/bin
#    #EMC2_TCL_DIR=$EMC2_HOME/tcl
#    #EMC2_TCL_LIB_DIR=$EMC2_HOME/tcl
#    #EMC2_LANG_DIR=$EMC2_HOME/src/objects
#    #EMC2_PO_DIR=$EMC2_HOME/share/locale
#    #EMC2_HELP_DIR=$EMC2_HOME/docs/help
#    #EMC2_RTLIB_DIR=$EMC2_HOME/rtlib
#    #LINUXCNC_CONFIG_PATH="~/linuxcnc/configs:$EMC2_HOME/configs"
#    #EMC2_NCFILES_DIR=$EMC2_HOME/nc_files
#    #REALTIME=$EMC2_HOME/scripts/realtime
#    #EMC2_IMAGE_DIR=$EMC2_HOME
#    #HALLIB_DIR=$EMC2_HOME/lib/hallib
#else()
#    #EMC2_HOME=${prefix}
#    #EMC2_BIN_DIR=${prefix}/bin
#    #EMC2_LATENCY_SCRIPT=$EMC2_BIN_DIR/latency-test
#    #EMC2_SCRIPT=$EMC2_BIN_DIR/linuxcnc
#    #EMC2_SUFFIX=""
#    #EMC2_ICON=linuxcncicon
#    #EMC2_TCL_DIR=${prefix}/lib/tcltk/linuxcnc
#    #EMC2_TCL_LIB_DIR=${prefix}/lib/tcltk/linuxcnc
#    #EMC2_LANG_DIR=${prefix}/lib/tcltk/linuxcnc/msgs
#    #EMC2_PO_DIR=${prefix}/share/locale
#    #EMC2_HELP_DIR=${prefix}/share/doc/linuxcnc
#    #case $MODULE_DIR in
#    #*/linuxcnc*) EMC2_RTLIB_DIR=$MODULE_DIR ;;
#    #*) EMC2_RTLIB_DIR=$MODULE_DIR/linuxcnc
#    #esac
#    #LINUXCNC_CONFIG_PATH="~/linuxcnc/configs:/usr/local/etc/linuxcnc/configs:"$(eval echo $EMC2_HELP_DIR)"/examples/sample-configs"
#    #EMC2_NCFILES_DIR=${prefix}/share/linuxcnc/ncfiles
#    #REALTIME=/etc/init.d/realtime
#    #EMC2_IMAGE_DIR=$(fe "$datadir")/linuxcnc
#    #HALLIB_DIR=${prefix}/share/linuxcnc/hallib
#endif()

add_subdirectory(src/rtapi)
add_subdirectory(src/hal)
add_subdirectory(src/hal/components)
add_subdirectory(src/hal/drivers)
add_subdirectory(src/hal/classicladder)
add_subdirectory(src/libnml)
add_subdirectory(src/module_helper)
add_subdirectory(src/emc)
add_subdirectory(scripts)
#add_subdirectory(tcl)
