#!/bin/zsh
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT
#
# Execute the callback-only D3D12 UMD ABI probe in an isolated ARM64 WinPE VM.
# This deliberately does not install a driver, open a dxgkrnl adapter, or touch
# Apple GPU hardware.  It validates the PE loader, OpenAdapter12, DDI tables,
# private-object ABIs, and the fake KMT/DXGI callback translations exercised by
# d12_umd_ddiprobe.exe.

set -euo pipefail

script_dir=${0:A:h}
umd_dir=${script_dir:h}
repo_root=${umd_dir:h:h:h:h}
asahi_workspace=${AURORA_ASAHI_WORKSPACE:-/Users/dj/Developer/Asahi/windows-on-apple-silicon}
qemu=${QEMU:-$asahi_workspace/build/qemu-aic2/qemu-system-aarch64}
firmware=${UEFI_CODE:-$asahi_workspace/build/vm/hvf-native-gic-smoke/firmware/edk2-aarch64-code.fd}
setup_tree=${WINDOWS_SETUP_TREE:-$asahi_workspace/build/vm/hvf-aic3-driver/iso-tree}
shared_build=${D12_UMD_MESA_BUILD:-$repo_root/_build-d3d12-umd-mesa-arm64-shared}
probe_build=${D12_UMD_BUILD_OUT:-$repo_root/_build-d3d12-umd-arm64}
umd_dll=${D12_UMD_DLL:-$shared_build/src/asahi/d3d12/d3d12agx_umd.dll}
callback_dll=${D12_UMD_CALLBACK_DLL:-$probe_build/d3d12agx_umd.dll}
ddiprobe=${D12_UMD_DDIPROBE:-$probe_build/d12_umd_ddiprobe.exe}
result_dir=${D12_UMD_QEMU_RESULTS:-$repo_root/_build-d3d12-umd-qemu-results}
qemu_lock=${QEMU_D12_DDIPROBE_LOCK:-/tmp/ntasi-qemu-d12-ddiprobe.lock}
timeout_seconds=${QEMU_TIMEOUT_SECONDS:-300}

for required in "$qemu" "$firmware" \
    "$setup_tree/sources/boot.wim" \
    "$setup_tree/efi/microsoft/boot/efisys_noprompt.bin" \
    "$umd_dll" "$callback_dll" "$ddiprobe"; do
   [[ -e $required ]] || { print -u2 "missing input: $required"; exit 2; }
done
for command_name in hdiutil lockf shasum wimlib-imagex xorriso; do
   command -v "$command_name" >/dev/null || {
      print -u2 "missing command: $command_name"
      exit 2
   }
done
[[ $timeout_seconds == <1-> ]] || {
   print -u2 'QEMU_TIMEOUT_SECONDS must be positive'
   exit 2
}

work=$(mktemp -d "${TMPDIR:-/tmp}/ntasi-d12-ddiprobe.XXXXXX")
stage=$work/stage
stage_image=$work/d12-ddiprobe-stage.dmg
device=
qemu_pid=
cleanup() {
   if [[ -n $qemu_pid ]] && kill -0 "$qemu_pid" >/dev/null 2>&1; then
      kill -TERM "$qemu_pid" >/dev/null 2>&1 || true
      wait "$qemu_pid" >/dev/null 2>&1 || true
   fi
   if [[ -n $device ]]; then
      hdiutil detach "$device" -force >/dev/null 2>&1 || true
   fi
   [[ -d $work ]] && rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$stage/results" "$result_dir"
cp -f "$umd_dll" "$stage/d3d12agx_umd.dll"
cp -f "$callback_dll" "$stage/d3d12agx_umd_callback.dll"
cp -f "$ddiprobe" "$stage/d12_umd_ddiprobe.exe"
print 'Aurora Silicon full-renderer D3D12 UMD clean-load callback probe v2' \
   > "$stage/NTASI-D12-DDIPROBE-STAGE.TAG"
(cd "$stage" && shasum -a 256 d3d12agx_umd.dll \
   d3d12agx_umd_callback.dll \
   d12_umd_ddiprobe.exe > SHA256SUMS.txt)

stage_mb=$(( $(du -sm "$stage" | awk '{print $1}') + 32 ))
hdiutil create -size "${stage_mb}m" -fs MS-DOS -volname NTASI_D12P \
   -layout NONE -ov "$stage_image" >/dev/null

attach_output=$(hdiutil attach -nobrowse "$stage_image")
device=$(print -r -- "$attach_output" | awk 'NR == 1 { print $1 }')
mount_point=$(print -r -- "$attach_output" | awk -F '\t' \
   'NF >= 3 && $NF ~ /^\/Volumes\// { m = $NF } END { print m }')
[[ -n $device && -d $mount_point ]] || {
   print -u2 'probe stage image did not mount'
   exit 3
}
COPYFILE_DISABLE=1 cp -R "$stage/." "$mount_point/"
/usr/sbin/dot_clean -m "$mount_point" 2>/dev/null || true
sync
hdiutil detach "$device" -force >/dev/null
device=

{
   print '@echo off'
   print 'setlocal EnableDelayedExpansion'
   print 'wpeinit'
   print 'set "MEDIA="'
   print 'for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist "%%D:\\NTASI-D12-DDIPROBE-STAGE.TAG" set "MEDIA=%%D:\\"'
   print 'if not defined MEDIA ('
   print '  wpeutil shutdown'
   print '  exit /b 20'
   print ')'
   print '"%MEDIA%d12_umd_ddiprobe.exe" --load-only "%MEDIA%d3d12agx_umd.dll" > "%MEDIA%results\\d12_umd_load.log" 2>&1'
   print 'if errorlevel 1 ('
   print '  copy /y "%MEDIA%results\\d12_umd_load.log" "%MEDIA%results\\d12_umd_ddiprobe.log" >nul'
   print '  echo 21>"%MEDIA%results\\d12_umd_ddiprobe.result"'
   print '  wpeutil shutdown'
   print '  exit /b 21'
   print ')'
   print '"%MEDIA%d12_umd_ddiprobe.exe" "%MEDIA%d3d12agx_umd_callback.dll" > "%MEDIA%results\\d12_umd_ddiprobe.log" 2>&1'
   print 'set "PROBE_RESULT=!ERRORLEVEL!"'
   print 'echo !PROBE_RESULT!>"%MEDIA%results\\d12_umd_ddiprobe.result"'
   print 'wpeutil shutdown'
   print 'exit /b !PROBE_RESULT!'
} > "$work/ntasi-d12-ddiprobe.cmd"
{
   print '[LaunchApps]'
   print '%SYSTEMROOT%\\System32\\cmd.exe, /c %SYSTEMROOT%\\System32\\ntasi-d12-ddiprobe.cmd'
} > "$work/winpeshl.ini"

cp -cR "$setup_tree" "$work/setup"
chmod -R u+w "$work/setup"
rm -f "$work/setup/sources/install.wim"
{
   print -r -- "add '$work/ntasi-d12-ddiprobe.cmd' '\\Windows\\System32\\ntasi-d12-ddiprobe.cmd'"
   print -r -- "add '$work/winpeshl.ini' '\\Windows\\System32\\winpeshl.ini'"
} | wimlib-imagex update "$work/setup/sources/boot.wim" 2 --check >/dev/null
xorriso -as mkisofs -iso-level 3 -full-iso9660-filenames -J -joliet-long -R \
   -volid NTASI_D12P_PE -eltorito-alt-boot \
   -e efi/microsoft/boot/efisys_noprompt.bin -no-emul-boot \
   -o "$work/d12-ddiprobe-winpe.iso" "$work/setup" >/dev/null 2>&1

set +e
lockf "$qemu_lock" "$qemu" \
   -machine virt,gic-version=3,its=off -accel hvf -cpu host -smp 4 -m 4G \
   -bios "$firmware" -device ramfb -device qemu-xhci,id=xhci \
   -drive "if=none,id=stage,format=raw,file=${stage_image:A},cache=unsafe" \
   -device usb-storage,drive=stage,removable=on \
   -drive "if=none,id=setup,format=raw,file=${work:A}/d12-ddiprobe-winpe.iso,readonly=on" \
   -device usb-storage,drive=setup,removable=on,bootindex=1 \
   -nic none -serial "file:$work/serial.log" -display none -no-reboot &
qemu_pid=$!
deadline=$(( SECONDS + timeout_seconds ))
timed_out=0
while kill -0 "$qemu_pid" >/dev/null 2>&1; do
   if (( SECONDS >= deadline )); then
      timed_out=1
      kill -TERM "$qemu_pid" >/dev/null 2>&1 || true
      break
   fi
   sleep 2
done
wait "$qemu_pid"
qemu_status=$?
qemu_pid=
set -e
cp -f "$work/serial.log" "$result_dir/serial.log" 2>/dev/null || true
if (( timed_out || qemu_status != 0 )); then
   print -u2 "D3D12 ddiprobe VM failed (timed_out=$timed_out qemu_status=$qemu_status)"
   print -u2 "serial log: $result_dir/serial.log"
   exit 3
fi

attach_output=$(hdiutil attach -nobrowse "$stage_image")
device=$(print -r -- "$attach_output" | awk 'NR == 1 { print $1 }')
mount_point=$(print -r -- "$attach_output" | awk -F '\t' \
   'NF >= 3 && $NF ~ /^\/Volumes\// { m = $NF } END { print m }')
[[ -n $device && -d $mount_point ]] || {
   print -u2 'probe result image did not mount'
   exit 3
}
cp -f "$mount_point/results/d12_umd_ddiprobe.log" \
   "$result_dir/d12_umd_ddiprobe.log" 2>/dev/null || {
   print -u2 'WinPE wrote no D3D12 ddiprobe log'
   exit 3
}
cp -f "$mount_point/results/d12_umd_load.log" \
   "$result_dir/d12_umd_load.log" 2>/dev/null || true
probe_result=$(tr -d '\r\n[:space:]' \
   < "$mount_point/results/d12_umd_ddiprobe.result" 2>/dev/null || print '')
hdiutil detach "$device" -force >/dev/null
device=

[[ -f $result_dir/d12_umd_load.log ]] && \
   cat "$result_dir/d12_umd_load.log"
cat "$result_dir/d12_umd_ddiprobe.log"
print -r -- "probe exit code: ${probe_result:-unknown}"
if [[ $probe_result == 0 ]] && \
    grep -q 'd12_umd_ddiprobe: PASS' "$result_dir/d12_umd_ddiprobe.log"; then
   print 'D3D12 UMD QEMU DDIPROBE: PASS'
   exit 0
fi
print -u2 'D3D12 UMD QEMU DDIPROBE: FAIL'
exit 1
