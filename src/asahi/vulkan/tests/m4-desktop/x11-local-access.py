#!/usr/bin/env python3
"""Allow/revoke one local X11 user; run as the existing display owner.

Equivalent to xhost +/-SI:localuser:USER without requiring the xhost package.
Never disables access control or authorizes remote clients. Revoke after tests.
"""
import argparse
import ctypes as C

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('action', choices=['allow', 'revoke'])
p.add_argument('--display', required=True)
p.add_argument('--user', required=True)
a = p.parse_args()
if not a.user or '\0' in a.user:
    p.error('invalid user')


class ServerAddress(C.Structure):
    _fields_ = [('typelength', C.c_int), ('valuelength', C.c_int),
                ('type', C.c_char_p), ('value', C.c_char_p)]


class HostAddress(C.Structure):
    _fields_ = [('family', C.c_int), ('length', C.c_int), ('address', C.c_void_p)]


x = C.CDLL('libX11.so.6')
x.XOpenDisplay.argtypes = [C.c_char_p]
x.XOpenDisplay.restype = C.c_void_p
for fn in [x.XAddHost, x.XRemoveHost]:
    fn.argtypes = [C.c_void_p, C.POINTER(HostAddress)]
x.XSync.argtypes = [C.c_void_p, C.c_int]
x.XCloseDisplay.argtypes = [C.c_void_p]
display = x.XOpenDisplay(a.display.encode())
if not display:
    raise RuntimeError('Cannot open display as its owner')
try:
    user = a.user.encode()
    si = ServerAddress(9, len(user), b'localuser', user)
    host = HostAddress(5, C.sizeof(si), C.addressof(si))  # FamilyServerInterpreted
    (x.XAddHost if a.action == 'allow' else x.XRemoveHost)(display, C.byref(host))
    x.XSync(display, False)  # Xlib's default error handler makes failures fatal.
finally:
    x.XCloseDisplay(display)
print(f'PASS: {a.action} SI:localuser:{a.user} on {a.display}')
