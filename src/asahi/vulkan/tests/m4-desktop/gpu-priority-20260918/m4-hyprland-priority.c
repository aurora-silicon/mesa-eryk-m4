/* Installed root-owned, mode 0755, cap_sys_nice=ep. Only launches the fixed
 * M4 compositor. Ambient transfer avoids AT_SECURE stripping its Mesa loader
 * environment. Patched Hyprland's main() drops ambient SYS_NICE before children
 * are launched; ordinary applications must not inherit it.
 */
#define _GNU_SOURCE
#include <linux/capability.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
int main(int argc, char **argv) {
  (void)argc;
  struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
  struct __user_cap_data_struct caps[2] = {{0}};
  if (syscall(SYS_capget, &header, caps)) {
    perror("capget");
    return 1;
  }
  caps[CAP_SYS_NICE / 32].inheritable |= 1u << (CAP_SYS_NICE % 32);
  if (syscall(SYS_capset, &header, caps) ||
      prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SYS_NICE, 0, 0)) {
    perror("SYS_NICE transfer");
    return 1;
  }
  /* The file-capability entry deliberately loses LD_LIBRARY_PATH. Restore
   * only our fixed, root-owned driver directory, never a caller's path. */
  if (setenv("LD_LIBRARY_PATH", "/opt/asahi-m4-port/lib", 1))
    return 1;
  argv[0] = "/opt/m4-hyprland/bin/Hyprland";
  execv(argv[0], argv);
  perror("Hyprland");
  return 1;
}
