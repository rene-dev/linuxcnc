add_library(inifile SHARED inifile.cc inivar.cc)
set_property(TARGET inifile PROPERTY POSITION_INDEPENDENT_CODE ON)
