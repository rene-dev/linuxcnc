cmake_minimum_required(VERSION 3.12)

project(linuxcnc VERSION 2.9.0)

include(CMakeDependentOption)

include_directories("include/")

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

# specify the C++ standard
# change to 20 as soon as possible
# (need gcc8, gcc7 allows 17)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# general options
option(BUILD_UDEV "deactivate this if udev is not available. Was previously named --disable-userspace-pci." ON)
option(TOOL_NML "Use nml for tool data, default is mmap" OFF)

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

# configure files
#configure_file(TutorialConfig.h.in TutorialConfig.h)

add_subdirectory(src/rtapi)
add_subdirectory(src/hal)
add_subdirectory(src/libnml)

