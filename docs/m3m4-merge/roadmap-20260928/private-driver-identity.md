# Explicit experiment driver identity

GPU experiment provenance now resolves explicit VK_DRIVER_FILES/VK_ICD_FILENAMES
JSON, its selected driver DSO, LD_LIBRARY_PATH graphics libraries, and absolute
LD_PRELOAD interposers. It hashes these alongside the retained default installation
and executable inputs. Device-tree board and boot version strings are included
when exposed. Resolution never loads a library or accesses GPU memory.

Missing or unresolved library search inputs are reported explicitly. Environment
assignments are declarations; they do not prove that an opaque wrapper obeyed the
selection. This is not a replacement for a workload's selected-device check.

`python3 tools/test-gpu-identity.py` covers absolute/relative/search-selected ICDs,
preload and unresolved inputs. A guarded 15-point M4 resource-allocation probe
passes with selected memory-final Mesa SHA f3141473a77b726bf41fb3b9c58979908bd58daed640bbe2d66f8032cd87e53b
recorded separately from the normal desktop library. The probe source's hash is
also recorded in the experiment manifest.
