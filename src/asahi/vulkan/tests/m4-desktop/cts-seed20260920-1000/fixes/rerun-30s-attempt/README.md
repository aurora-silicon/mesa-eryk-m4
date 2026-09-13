# Longer-timeout diagnostic attempt, stopped after 854 cases

740 Pass, 113 NotSupported, one Timeout; 146 not run in this attempt. Case 759
still stalled with a 30-second case limit, so increasing the limit did not fix
it. A subsequent isolated WAYLAND_DEBUG trace showed that default-queue
configure events and pings were not dispatched during the blocking Vulkan
render loop. The xdg-shell event-pump correction and its separately named
binary are recorded alongside this attempt. No Mesa changes between these
reruns. Full per-case evidence remains at the paths in summary.json.
