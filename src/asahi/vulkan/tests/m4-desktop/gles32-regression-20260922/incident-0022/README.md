# GPU timeout in segment22: unresolved

Evidence:20260922T161132262747Z-gles32-plan-0022-r0. Kernel job7013074 stalled
in Tiling, context51, zero render timestamps. Queue/pipe indices advanced,
but retirement remained unproven; DMA owners retained. Do not bypass retirement
checks. Sealed1,681,584-byte firmware dump retained; SHA256
12476656ad90c8fd381dedc9992c136cad3f7e68d9984963c781599b32efdb02.

Four CTS workers active, so no single case is proven responsible. Seven image
comparison failures preceded the timeout. All seven previously Passed. Incident
peak82C; no thermal failure. Target recovered from bootf9d383d2... to
84d6165b-76e2-4a35-8ab5-e93c7b9e705f. Desktop/autologin active, watchdog idle60.
Four interrupted cases pass serial recovery probe on unchanged drivers; this
establishes recovery only, not resolution of the incident.

Campaign stopped with53,899 completed:52,211Pass,1,669NS,10QualityWarning,
2CompatibilityWarning,7Fail;4Interrupted,272,699Unrun. Controller count53,454
reflects only fully completed segments; summary.json includes the partial segment.
Further retests and any continuation must preserve this incident and failures.

The seven image failures also all Pass in the serial post-reboot recheck on
unchanged drivers. This does not erase their original Fail outcomes or explain
the GPU timeout. A separate repeat of the exact2500-case parallel batch follows.

Exact2500-case four-worker repeat after reboot:2466Pass/34NS, guardPass,
peak91C,27.34s. Evidence20260922T161530848091Z-gles32-plan-0022-r0.
Coverage continuation PID1887131 runs original unrun/interrupted peers only;
segment22 uses one worker under explicit recovery-review-0022.json. Subsequent
segments retain the original plan. Original seven Fail outcomes remain in the
main campaign; passing diagnostic rechecks are separate. GPU incident OPEN.
