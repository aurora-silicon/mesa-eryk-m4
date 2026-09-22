# Third retained GPU incident: firmware crash in segment278

Single-worker run completed200 cases, then stopped with one Interrupted/929Unrun.
Active global310186: KHR-GLES32.core.tessellation_shader.vertex.vertex_spacing_primitive_mode_isolines_vs_mode_fractional_even_spacing.
Kernel packet2550601 preparation failed bank0/index1/count7/EBUSY. Later RTKit crashed:
DAG: Non Sequential Stamp Updates seen entryIdx0x22 roots.dag0x21 stampIdx0x70
stampValue0x801ca200 channel0xfffffc2000368000 channelRingCommandIndex0x47.
Job2550602 finished EIO with retirement unproven; DMA owners retained and sealed
1,681,584-byte snapshot collected. Guard verdict gpu_fault, peak98.25C, no thermal stop.
This is distinct from the earlier two tiling-timeout incidents.

Recovery boot41f12c43-a033-4c7a-9c18-8bb622c6815e, same kernel/libraries/CTS.
Desktop/autologin/watchdog60 restored. Active case Pass serially; exact1130-case
serial repeat Pass1129/NS1 in59.3s. Raw interrupted attempt and firmware incident
remain retained. Review permits only930 unfinished cases, not replacement of a
terminal case result. No driver fix or definitive process attribution claimed.

Offline source lead: g16_runtime.rs prepare_command advances global stamps and
ClientNotifications::reserve before fallible work construction; publish propagates
preparation errors without an obvious reservation rollback. Cached Render::clear
can return EBUSY when UMA completion differs. The observed error source and the
correct rollback/lifetime contract need validation; this is a lead, not a patch.

Runner recovery now accepts gpu_fault only when the review explicitly specifies
that exact verdict and evidence. Existing reboot/hash/serial-Pass requirements
remain; the default review verdict remains gpu_timeout. Main coverage unchanged.
