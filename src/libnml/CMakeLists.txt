set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(LIBNML_SOURCES
        rcs/rcs_print.cc
        rcs/rcs_exit.cc

        os_intf/_sem.c
        os_intf/_shm.c
        os_intf/_timer.c
        os_intf/sem.cc
        os_intf/shm.cc
        os_intf/timer.cc

        buffer/locmem.cc
        buffer/memsem.cc
        buffer/phantom.cc
        buffer/physmem.cc
        buffer/recvn.c
        buffer/sendn.c
        buffer/shmem.cc
        buffer/tcpmem.cc

        cms/cms.cc
        cms/cms_aup.cc
        cms/cms_cfg.cc
        cms/cms_in.cc
        cms/cms_dup.cc
        cms/cms_pm.cc
        cms/cms_srv.cc
        cms/cms_up.cc
        cms/cms_xup.cc
        cms/cmsdiag.cc
        cms/tcp_opts.cc
        cms/tcp_srv.cc

        nml/cmd_msg.cc
        nml/nml_mod.cc
        nml/nml_oi.cc
        nml/nml_srv.cc
        nml/nml.cc
        nml/nmldiag.cc
        nml/nmlmsg.cc
        nml/stat_msg.cc

        linklist/linklist.cc
        )

add_library(nml SHARED ${LIBNML_SOURCES})
target_link_libraries(nml PRIVATE hal)
target_link_libraries(nml PRIVATE ${TIRPC_LIBRARIES})
target_include_directories(nml PRIVATE ${TIRPC_INCLUDE_DIRS})
get_uspace_config(nml)

set_property(TARGET nml PROPERTY POSITION_INDEPENDENT_CODE ON)
