SET(CMAKE_C_COMPILER clang)
SET(CMAKE_CXX_COMPILER clang++)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Project wide compiler flags, set specific flags for each target if needed.
set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} "-Wall")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_DEBUG "-fvisibility=default -O0 -g")

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-Wall")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS_DEBUG "-fvisibility=default -O0 -g")
