# Staged AppleAgxWddm package integration

`stage_package.py` installs the native ARM64 D3D12 UMD into a throwaway
AppleAgxWddm package directory. It preserves the existing DX9/DX10/DX11
Triton registrations and adds `d3d12agx_umd.dll` only at KMTUMDVERSION index
3 (D3D12). It also updates `SourceDisksFiles`, `CopyFiles` through the existing
`AppleAgxWddm_UMD` section, and the MakeCat CDF.

Run it after the Aurora package builder has copied the INF and CDF into its
stage, and before MakeCat hashes the package:

```powershell
python stage_package.py `
  --package-dir C:\stage\package `
  --dll C:\build\d3d12agx_umd.dll
```

The tool accepts only an ARM64 PE image, removes any stale catalog, and tells
the caller to regenerate/sign the catalog. It intentionally does not edit the
live KMD source checkout, which allows paging/display work on that checkout to
continue independently.

When a staged UMD bundle deliberately pins a previously validated KMD package
while the live KMD tree has advanced, `check-inf-umd-registration-pinned.py`
runs the live tree's complete registration contract with only `DriverVer`
overridden through `AURORA_EXPECTED_DRIVER_VER`. Point
`AURORA_INF_REGISTRATION_BASE` at Aurora's authoritative checker and
`AURORA_UMD_REGISTRATION_CHECKER` at this wrapper; malformed or absent identity
inputs fail closed.

For bring-up, copy `d12_umd_ddiprobe.exe`, `d12_umd_hwprobe.exe` and
`d12_umd_runtime_probe.exe` from the strict ARM64 build into a separate tools
directory beside the package. They are deliberately not catalog members. Run
the loader/DDI probe first, then the KMT hardware probe, and only then the
Microsoft-runtime upload/default/readback probe. Do not run these while another
owner is actively cycling or testing the Windows target.
