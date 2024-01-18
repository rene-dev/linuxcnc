add_executable(hy_vfd hy_vfd.c hy_comm.c)
target_link_libraries(hy_vfd
        PRIVATE
        hal
)
get_uspace_config(hy_vfd)
