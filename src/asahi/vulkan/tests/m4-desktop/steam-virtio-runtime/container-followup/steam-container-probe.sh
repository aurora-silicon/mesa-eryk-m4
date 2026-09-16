#!/bin/bash
# Run inside Steam's unmodified pressure-vessel entry point, under a GPU guard.
set -eu
printf 'Container graphics environment:\n'
env | sort | sed -n '/^\(FEX\|LD_LIBRARY\|LIBGL\|VK_\|GALLIUM\|__EGL\)/p'
find /usr/lib/pressure-vessel/overrides -name '*gbm*' -ls || true
/usr/bin/FEXInterpreter /lib64/ld-linux-x86-64.so.2 --list /home/e/.local/share/Steam/ubuntu12_64/steamwebhelper
printf 'Checking FEX child startup in a nested user/network namespace:\n'
/usr/bin/unshare --user --map-root-user --net /usr/bin/FEXInterpreter /usr/bin/true
printf 'PASS: FEX child in nested user/network namespace\n'
exec /home/e/.cache/m4-qualification/glx-probe64
