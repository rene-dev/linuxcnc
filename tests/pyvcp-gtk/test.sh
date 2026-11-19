#! /usr/bin/bash

# pyvcp-gtk must create exactly the pins pyvcp creates. 'expected' is
# the output of the same panel loaded with pyvcp.

set -e

xvfb-run halrun -s do-test.hal
# Ubuntu's xvfb-run merges stderr into stdout, so on a slow machine the
# halcmd "Waiting for component" pacifier can end up in result. Drop it.
sed -i -e '/^Waiting for component/d' -e '/mypanel/!d' result

exit 0
