# Shared pool control, September 19

Kerneld7b7282e7c79 separates pools from scenes. Normalprivatepath152212
retains6.156..6.254ms. Experimentalcomputeoverlap nowsharesonephysicalpool
withaggregatecapacity40MiB for10scenes, exactlyoneInitBuffer perpacket.
152300 passes everypixel andtenquerytotals,6.844..6.919ms. This is still
worse than normal, so leave the option disabled. Allocationperpacket needs
a retainedpoolcache before any defaultconsideration. Rawreports retained.

Independent completion/TA and host-retirement correction153923 pass exact
pixels/counts at6.716..6.890ms. The earlier153314 timeout's decoded dump
andguardreport are retained; ownFS andUSC hadalreadycompleted.

HK_M4_INDEPENDENT_TILING=true marks eligible adjacent render transitions
withtheextendedpermissionwithoutrequestingcompute-boundaryoverlap.
Replay154248 passes everypixel/count at6.126..6.253ms, essentiallyunchanged
fromnormal. BothindependentandcomputeoverlapoptionsremainOFFbydefault.
Privatecandidate library9231de6a, no normalMesa installation change.
