# G16G unsigned 64-bit atomic min/max subset

The compiler supports the witnessed device-memory unsigned 64-bit min/max
forms **without returned values**. This is a compiler subset, not full Vulkan
64-bit atomic support. Buffer/shared/image 64-bit atomic feature bits remain
disabled by default. Returning operations, arithmetic, exchange and compare
exchange require further implementation and qualification.

Apple Metal917 compiles `atomic_min_explicit` and `atomic_max_explicit` on
`device atomic_ulong` for `applegpu_g16g`. Their 14-byte `atomicdev` encodings
are retained in `atomic64-minmax/native-and-oracles.tar.gz`. The generated
table decoder reproduces each byte exactly: field90 is3 instead of the 32-bit
form's1, field88 remains unsigned min/max4/6, and field81 selects the standalone
family. The common register operand form remains field10=2. These forms use
four consecutive 16-bit registers and do not return through TPS. Address
lowering and final encoding now agree on eight-byte element indices.

The encoder rejects other 64-bit operations, returned results and AGX3c.
The compiler drops the unqualified 64-bit discard hint. Host tests reproduce
both complete native instructions and reject unsupported combinations. This
record establishes no hardware success on another Apple GPU.

Metal917 rejects 64-bit fetch-add, returning fetch-min/max, and direct internal
fetch-add calls. Those failures are archived. The current Xcode launchers
require accepting a new license, so offline compilation instead uses the
already installed standalone917 frontend/backend and CLT Python. No license
state was changed. `native-metal-compile.py` now accepts explicit frontend and
remote Python paths; normal defaults are unchanged.

Six independent configurations check min/max on 256 distinct addresses, one
contended address, and eight contended addresses across four workgroups. Full
buffer comparisons include high-bit and carry-boundary values and untouched
regions. All six pass on lavapipe and M4
(`20260916T130428636023Z-dx-atomic64-minmax-r1`, peak61.25 C). Hardware library:
`f3ef24b71421838b72232ee4fec991b2ba188a0a92b55fdd904ef8b4570500e7`.
It used the retained temporary feature gate solely for these supported
operations. The initial single-address minimum started at zero and could
pass on a noop shim; the final probe starts at a nontrivial value so both
operations must change memory. Host shim checks compile the shaders but do
not establish rendering or atomic execution.

Hardware execution retains the 60 s watchdog and six-channel 0.5 s thermal
monitor with95 C cutoff. Broader regression uses12 workers and the unchanged
60 s case bound. This does not enable SM6.6 or establish Steam readiness.

The ordinary 1,001-case regression is unchanged: **815 Pass / 186
NotSupported / zero failures** (`20260916T130945184351Z-dx-atomic64-regression-r1`),
peak64 C. Host Asahi suite:104 Pass / the same11 known failures.
