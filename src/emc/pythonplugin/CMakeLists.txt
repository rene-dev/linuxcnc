add_library(pythonplugin SHARED python_plugin.cc)
target_link_libraries(pythonplugin Python3::Module)
target_link_libraries(pythonplugin Boost::${BOOSTPYTHON})