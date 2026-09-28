#!/usr/bin/env python3
"""Arrange override-redirect CTS windows on a PRIVATE headless Xwayland display.

CTS Xlib creates every window at (0, 0). Weston occludes all but the top
window and Xwayland falls back to 2-second presentation ticks. Moving these
windows without resizing keeps their presentation callbacks live. Never use
this on the user's display. The parent headless context owns this process.
"""
import ctypes as C
import json
import os
import time

assert os.environ['XDG_RUNTIME_DIR'].startswith('/run/m3-cts-headless-')
x = C.CDLL('libX11.so.6')
D = C.c_void_p
W = C.c_ulong
I = C.c_int
U = C.c_uint
class Attr(C.Structure):
    _fields_ = [('x', I), ('y', I), ('width', I), ('height', I),
                ('border_width', I), ('depth', I), ('visual', D), ('root', W),
                ('class_', I), ('bit_gravity', I), ('win_gravity', I),
                ('backing_store', I), ('backing_planes', W), ('backing_pixel', W),
                ('save_under', I), ('colormap', W), ('map_installed', I),
                ('map_state', I), ('all_event_masks', C.c_long),
                ('your_event_mask', C.c_long), ('do_not_propagate_mask', C.c_long),
                ('override_redirect', I), ('screen', D)]
for name, args, result in [
    ('XOpenDisplay', [C.c_char_p], D), ('XDefaultRootWindow', [D], W),
    ('XQueryTree', [D, W, C.POINTER(W), C.POINTER(W), C.POINTER(C.POINTER(W)), C.POINTER(U)], I),
    ('XGetWindowAttributes', [D, W, C.POINTER(Attr)], I),
    ('XMoveWindow', [D, W, I, I], I), ('XSync', [D, I], I),
    ('XFree', [D], I), ('XCloseDisplay', [D], I),
]:
    fn = getattr(x, name); fn.argtypes = args; fn.restype = result
# Windows can disappear between query and attributes during normal CTS teardown.
@C.CFUNCTYPE(I, D, D)
def error(display, event):
    return 0
x.XSetErrorHandler.argtypes = [type(error)]
x.XSetErrorHandler(error)
d = x.XOpenDisplay(None)
assert d, 'Cannot open private Xwayland display'
root = x.XDefaultRootWindow(d)
def layout(windows):
    """Fresh live-window layout: XIDs can be reused between CTS processes."""
    positions = {}
    cursor_x, cursor_y, row_height = 16, 40, 0
    for window, width, height in sorted(windows):
        if cursor_x + width > 1024:
            cursor_x, cursor_y, row_height = 16, cursor_y + row_height + 16, 0
        if cursor_x + width > 1024 or cursor_y + height > 1024:
            return None  # Never resize or silently move a test off screen.
        positions[window] = (cursor_x, cursor_y)
        cursor_x += width + 16
        row_height = max(row_height, height)
    return positions

last_unavailable = None
try:
    while True:
        r = W(); p = W(); children = C.POINTER(W)(); count = U()
        windows = {}
        if x.XQueryTree(d, root, C.byref(r), C.byref(p), C.byref(children), C.byref(count)):
            for window in list(children[:count.value]):
                a = Attr()
                if not x.XGetWindowAttributes(d, window, C.byref(a)):
                    continue
                if a.override_redirect and a.class_ == 1 and a.map_state == 2 and a.width >= 16 and a.height >= 16:
                    windows[window] = a
            if children: x.XFree(children)
            signature = tuple(sorted((w, a.width, a.height) for w, a in windows.items()))
            positions = layout(signature)
            if positions is None:
                if signature != last_unavailable:
                    print(json.dumps(dict(layout_unavailable=signature)), flush=True)
                last_unavailable = signature
            else:
                last_unavailable = None
                for window, position in positions.items():
                    a = windows[window]
                    if (a.x, a.y) == position:
                        continue
                    x.XMoveWindow(d, window, *position)
                    x.XSync(d, False)
                    actual = Attr()
                    observed = ([actual.x, actual.y] if x.XGetWindowAttributes(d, window, C.byref(actual)) else None)
                    print(json.dumps(dict(window=window, original=[a.x,a.y,a.width,a.height],
                                          requested=position, observed=observed)), flush=True)
        time.sleep(.01)

finally:
    x.XCloseDisplay(d)
