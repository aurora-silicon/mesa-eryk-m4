# M3 Xlib multi-swapchain presentation timeout

Original catalog index 3140212:
dEQP-VK.wsi.xlib.maintenance1.deferred_alloc.fifo.bind_image_multi_swapchain.
The 60-second case bound expired with GPU health intact. Original Timeout
and all Unrun records remain in segment 3140202-3140218; no deadline increase.

The two override-redirect Xlib windows both start at (0,0), 128x256. Moving
only their positions to (16,40)/(160,40) in the private headless compositor
lets both receive ordinary presentation callbacks. Isolated case passes;
20 consecutive cases in one compositor pass in six seconds, with XGetWindowAttributes
confirming each move, including repeatedly reused XIDs. The complete original
16-case selection then returns five Pass, eleven NotSupported. The failed case
now takes 0.225 s. No driver, CTS case or surface-size changes.

The previous opt-in helper retained a permanent seen-XID set and accumulated
coordinates across destroyed windows. It could ignore a later client reusing
an XID or eventually move windows off screen. Layout now recomputes from live
mapped override-redirect windows, verifies actual coordinates, and refuses to
resize or silently place a layout outside the 1024x1024 test output. It leaves
managed windows alone. Layout inability is logged, never treated as successful
placement. Xlib selections enable this environment by default; explicit
M3_CTS_TILE_X11=0 retains the occluded diagnostic. Environment and helper hash
are recorded. This affects only the private test compositor, not the desktop.

Do not transfer this diagnosis to the still-open intermittent XCB case: its
windows are managed and this helper does not move them. Original XCB failure
remains recorded despite 80 passing follow-up trials.

Evidence: build/driver-roadmap-20260928/m3-xlib-{multi-tiled,tiled-repeat,
original-sequence-tiled}; original full-catalog segment above. The original
sequence and repeated placement receipts are retained alongside this note.
