# Running checkpoint: 100,357 completed pairs

Campaign continues; 226,245 pairs remain unrun. Raw counts:
{"CompatibilityWarning": 18, "Fail": 7, "NotSupported": 2348, "Pass": 97969, "QualityWarning": 15, "Unrun": 226245}.

Seven original image Fail outcomes remain retained. All seven pass serially
and in the exact parallel batch after reboot; see incident-0022. The associated
GPU timeout is unresolved, and this passing continuation is not a fix claim.
No further Fail or GPU incident through this checkpoint. One Pass->CompatibilityWarning
flush timing case remains pending isolated recheck; other warnings/skips match
the retained historical baseline. All post-recovery guards pass.
