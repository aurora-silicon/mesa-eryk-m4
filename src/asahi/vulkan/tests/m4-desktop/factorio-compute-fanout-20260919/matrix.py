import subprocess
for count in (1, 9, 10, 64):
 for flags in range(8):
  print(f"CASE count={count} flags={flags}", flush=True)
  subprocess.run(['/root/vulkan-compute-timestamp-copies-probe','18',str(flags),str(count)], check=True, timeout=8)
print('COMPUTE_TSCOPIES_MATRIX_PASS', flush=True)
