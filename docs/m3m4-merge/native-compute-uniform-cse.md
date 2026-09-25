# Late native compute uniform copy elimination

Apply the existing post-legalization CSE pass to ordinary compute shaders as
well as fragment shaders. Native operand legalization materializes uniforms
into SSA temporaries after the normal CSE pass. These repeated reads may be
shared within a basic block because application shader uniforms are immutable.
Keep constant programs (which write uniforms) and internal helpers excluded.

Validation: all 296 backend tests pass. The unchanged native Vulkan gameplay
capture completes 30 measured frames on M3 and M4, with all three screenshots
RGB-exact against the control. The longest observed compute executable shrinks
from 591262 to 569702 bytes (3.6%), while retaining its register and scratch
counts. The instrumented replay time is essentially unchanged; this is a code
size improvement, not a demonstrated FPS improvement.

Guarded evidence: M4 20260925T165913787277Z-satisfactory-native-compute-cse;
M3 20260925T170136031670Z-m3-satisfactory-vulkan-gameplay-check. Both used
watchdog and thermal guards. Game compiler inputs/images remain private in the
local benchmark repository.
