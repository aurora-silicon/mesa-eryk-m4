# Reviewed ES32 case timeout at segment 105

Four-worker original: 809 Pass, 1 NotSupported, 1 Timeout, 3 canceled SIGKILL
workers initially labeled Crash, 2 Interrupted, 314 Unrun. The point-mode case
(global 158994) exceeded the unchanged 60-second bound at 60.056 s. No GPU fault,
thermal guard failure, OOM or memory shortage; minimum available memory 7448.9 MiB.
Same-boot serial recheck: all six cases Pass; point-mode 11.135 s. Earlier parallel
configurations ran this case in 50–58 s, consistent with contention sensitivity.
This is not proof of a driver fix or a diagnosis of the separate segment22 GPU fault.

Original timeout stays terminal. Review reclassifies coordinated-kill peers as
Interrupted; continuation runs only 319 unfinished cases. Future 25 segments
containing the point-mode case use one worker. Catalog, indices, binaries, settings
and time bounds remain pinned. A new fatal result still stops the controller.
