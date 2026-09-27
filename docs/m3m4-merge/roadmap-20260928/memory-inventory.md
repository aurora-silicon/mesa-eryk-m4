# Memory ownership inventory, first pass

This is a working inventory. It does not close the roadmap's complete allocation,
pressure, accounting or cache audit. Source paths below are under
`drivers/gpu/drm/asahi/` unless identified as Mesa.

| Storage | Owner/publication | Release or retention | Accounting today |
| --- | --- | --- | --- |
| User and kernel GEM extents (`gem.rs`) | `AsahiObject`, backing shmem object; mapping/handle refs may share it | Last object reference, not first handle close; active VM jobs can defer unbind | New current/peak extent bytes and object count, split user/kernel |
| M3 coherent firmware buffers (`m3_memory.rs`) | Buffer owns coherent backing and firmware/GPU mappings; config/native jobs retain Buffer | Mapping fields precede backing in drop order; failed runtime graph remains held | New coherent current/peak extent bytes and object count |
| M3 paged tiler/private backing | Buffer retains common GEM object and two UAT mappings | Retired rebuild replaces whole owner; allocation failure keeps old owner | Included once in kernel GEM, not coherent counter |
| M4 firmware/config, command/private buffers (`g16_memory.rs`) | Kernel GEM owner plus firmware/GPU mappings | Active job and cached previous-slot owners retain them | Included in kernel GEM; mapping aliases do not add extent bytes |
| M4 parameter storage (`g16_parameter.rs`) | Per-pass/shared Pool references with disjoint scene bookkeeping | Retired render owners or bounded owner cache | GEM extents visible; per-pool live/high-water subdivision still needed |
| M4 idle owner caches (`g16_owner_cache.rs`) | Registry references and per-client queues | Capacity policy/eviction; queue close stops cache repopulation; last active slot retained separately | Existing cache bytes/hits/misses/evictions plus GEM extents |
| UAT page tables and VM bookkeeping (`mmu.rs`, `pgtable.rs`) | VM, mappings, GPU job guard, reservations | Deferred unmap until accepted jobs retire; failed jobs retain references | Page-table/pinned-page counters still needed |
| Timestamp views / attachment lists / packets | Packet plus scheduler/dependency refs | Success releases job guard; failure retains unretired DMA ownership | GEM extent covers backing only; metadata allocations not yet accounted |
| RTKit shared crash memory | Reserved firmware-data region, validated CPU mapping | RTKit teardown after ASC stop, otherwise retained runtime | Reserved region is not dynamically allocated GEM; not in counters |
| Fault snapshots (`g16_fault.rs`) | One bounded KVVec offered to devcoredump | Framework timeout/release callback | 2 MiB M3 / 16 MiB M4 cap; first-fault copy size in receipt; not GEM |
| Mesa command arena, descriptors, shader binaries | BO/device/command lifetime machinery (`hk_cmd_lifetime.c`, descriptor pools, pipelines) | Vulkan object/command/reference lifetimes, caches | Kernel user-GEM extents see BOs, not Mesa suballocations or allocator metadata |
| Mesa shader/pipeline caches and compiler CPU IR | Mesa device, caches, transient compile jobs | Cache eviction, object/device destruction | Needs separate CPU/cache accounting and pressure tests |

## Counter contract

`/sys/kernel/debug/asahi-{m3,m4}/memory` is a private versioned observation file.
The allocation extent is the page-aligned object size, not resident pages,
pinned pages, VRAM use, or a claim about unique imported physical memory. GEM
construction/unwind is included, so a transient failed construction may raise a
peak. Counters use fixed atomic storage, and tickets perform no allocations.
They reject counter overflow rather than wrapping. Objects retained after a GPU
fault remain counted because their last backing-owner reference is still held.

Each field is observational. Concurrent allocation/free can make counts and
bytes reflect slightly different instants; compare quiescent current values for
leak tests. Lifetime peak values never decrease. The counts reset only with a
new module instance; the progress endpoint identifies runtime generation.

The SG import path, CPU virtual mappings, dma-buf sharing and individual pinned
page accounting remain separate audit items. These counters do not establish
safe reclamation or stopped DMA.

## GEM size boundary

Both user and kernel constructors previously rounded with unchecked addition.
Near-usize-max requests could wrap (or panic with overflow checks) before the
shmem helper saw the intended size. `checked_object_size` rejects zero and
checks the addition before rounding to 16 KiB. Host tests cover every overflowing
size in the final page and ordinary boundary sizes, with optimized and checked
Rust builds. Live ioctl validation is recorded separately; no enormous valid
allocation is needed to test these invalid requests.
