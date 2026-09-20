# M3 CTS repair 02

Unchanged M4 CTS, same selected baseline cases: five Pass, one Fail (primitive restart). Both previous invalid-vertex/index buffer GPU faults now pass with kernel soft-fault advertising disabled for the owned M3 runtime. The texture query path preserves the dynamic Gen4 sampler bias rather than requiring HAL200 zero bias. Array-gradient dynamic offsets use the already implemented shared layer/offset staging form.

Guard complete, peak 54.75 C; full QPAs and thermal/kernel logs in build/m3-cts-repair-02 and target /root/m3-cts-repair-02. Exact binaries and baseline indices recorded alongside results.
