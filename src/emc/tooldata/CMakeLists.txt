add_library(emc_tooldata
        tooldata_common.cc
        tooldata_db.cc
        tool_watch.cc)
target_include_directories(emc_tooldata PUBLIC .)
target_link_libraries(emc_tooldata PRIVATE ulapi emc_nmlintf)
get_uspace_config(emc_tooldata)
set_property(TARGET emc_tooldata PROPERTY POSITION_INDEPENDENT_CODE ON)

if(${TOOL_NML})
    target_sources(emc_tooldata PRIVATE
            tooldata_nml.cc
    )
else()
    target_sources(emc_tooldata PRIVATE
            tooldata_mmap.cc
    )
endif()

add_executable(tooldata_watch tool_watch.cc)
target_link_libraries(tooldata_watch
        PRIVATE
        Python3::Python
        emc_ini
        emc_tooldata
        nml
        emc_nmlintf)
get_uspace_config(tooldata_watch)
