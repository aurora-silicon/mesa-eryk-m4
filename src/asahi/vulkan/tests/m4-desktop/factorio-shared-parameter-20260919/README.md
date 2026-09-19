# Shared pool control, September 19

Kerneld7b7282e7c79 separates pools from scenes. Normalprivatepath152212
retains6.156..6.254ms. Experimentalcomputeoverlap nowsharesonephysicalpool
withaggregatecapacity40MiB for10scenes, exactlyoneInitBuffer perpacket.
152300 passes everypixel andtenquerytotals,6.844..6.919ms. This is still
worse than normal, so leave the option disabled. Allocationperpacket needs
a retainedpoolcache before any defaultconsideration. Rawreports retained.
