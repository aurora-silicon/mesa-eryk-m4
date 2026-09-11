#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Give seatd-launch's private socket to the selected desktop account."""
import os
import pwd
import stat
import sys

account = pwd.getpwnam(sys.argv[1])
path = os.environ['SEATD_SOCK']
info = os.lstat(path)
if os.geteuid() != 0 or not account.pw_uid or not stat.S_ISSOCK(info.st_mode) or info.st_uid != 0:
    raise RuntimeError('Expected the root-owned private seatd socket and a non-root desktop user')
os.chown(path, account.pw_uid, account.pw_gid)
# runuser preserves cwd. Inheriting the root SSH launch directory makes
# ordinary menu-launched terminals fail before they can start their shell.
os.chdir(account.pw_dir)
os.execvp('runuser', ['runuser', '-u', account.pw_name, '--', *sys.argv[2:]])
