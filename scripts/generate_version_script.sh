#/bin/sh
#
# this shell script generates the version script from a given object file.
# this is used for module generation, so the rtapi_export symbols are
# correctly exported in the binary.
#
# As with RTAI a linux kernel module is built, some information is
# exported using the MODULE_INFO kernel macros.
#
# In userspace these macros are implemented by rtapi. This information
# is exported in the rtapi_export section and afterwards read when
# halcmd reads these files with dlopen.
#
# This way halrun / halcmd can display information about the loaded
# modules.
#
# Usage:
# generate_version_script.sh objfile.o > objfile.ver
#

objcopy -j .rtapi_export -O binary $1 $1.sym
(echo '{ global : '; tr -s '\0' < $1.sym | xargs -r0 printf '%s;\n' | grep .; echo 'local : * ; };')
