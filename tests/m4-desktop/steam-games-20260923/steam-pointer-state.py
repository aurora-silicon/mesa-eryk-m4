#!/usr/bin/env python3
"""Read X11 pointer coordinates for a specified host window ID."""
import ctypes as C,ctypes.util,json,sys
x=C.CDLL(ctypes.util.find_library('X11'))
x.XOpenDisplay.argtypes=[C.c_char_p];x.XOpenDisplay.restype=C.c_void_p
x.XQueryPointer.argtypes=[C.c_void_p,C.c_ulong,C.POINTER(C.c_ulong),C.POINTER(C.c_ulong),C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_uint)]
d=x.XOpenDisplay(None);assert d
root=C.c_ulong();child=C.c_ulong();rx=C.c_int();ry=C.c_int();wx=C.c_int();wy=C.c_int();mask=C.c_uint()
r=x.XQueryPointer(d,int(sys.argv[1],0),C.byref(root),C.byref(child),C.byref(rx),C.byref(ry),C.byref(wx),C.byref(wy),C.byref(mask))
print(json.dumps(dict(ok=r,root=[rx.value,ry.value],window=[wx.value,wy.value],mask=mask.value)))
