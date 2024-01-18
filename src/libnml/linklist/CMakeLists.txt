add_library(libnml_linklist
        linklist.cc
        )

target_include_directories(libnml_linklist PUBLIC .)
set_target_properties(libnml_linklist PROPERTIES POSITION_INDEPENDENT_CODE ON)
