set(RS274NGC_SOURCES
        interp_arc.cc
        interp_array.cc
        interp_base.cc
        interp_check.cc
        interp_convert.cc
        interp_queue.cc
        interp_cycles.cc
        interp_execute.cc
        interp_find.cc
        interp_internal.cc
        interp_inverse.cc
        interp_read.cc
        interp_write.cc
        interp_o_word.cc
        interp_g7x.cc
        modal_state.cc
        nurbs_additional_functions.cc
        interp_namedparams.cc
        interp_python.cc
        interp_remap.cc
        interp_setup.cc
        canonmodule.cc
        pyparamclass.cc
        pyemctypes.cc
        pyinterp1.cc
        pyblock.cc
        pyarrays.cc
        interpmodule.cc
        rs274ngc_pre.cc
        interp_inspection.cc
        )

add_library(rs274ngc SHARED ${RS274NGC_SOURCES})
target_link_libraries(rs274ngc PRIVATE Python3::Module)
target_link_libraries(rs274ngc PRIVATE Boost::${BOOSTPYTHON})
target_link_libraries(rs274ngc PRIVATE pythonplugin)
get_uspace_config(rs274ngc)