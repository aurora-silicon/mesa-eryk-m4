# First complete rerun after fixes, original 20-second bound

All 1,000 original cases attempted once with 20 workers and the corrected CTS
binary. 870 Pass, 129 NotSupported, one Timeout (Wayland case 759). No Fail,
Crash, ResourceError or GPU fault. The campaign stopped on the timeout after
852 cases; inspection confirmed guard success and unchanged boot, then the
remaining 148 ran in a separate segment. The timeout is retained, not replaced
by the separate 30-second-bound campaign. The Wayland test renders 600 frames
with FIFO presentation and already took 17.758 seconds when run alone.
