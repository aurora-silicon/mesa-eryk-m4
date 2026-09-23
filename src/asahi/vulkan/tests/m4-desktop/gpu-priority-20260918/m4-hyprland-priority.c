/* Installed root-owned, mode 0755, cap_sys_nice=ep. Only launches the fixed
 * M4 compositor. Ambient transfer avoids AT_SECURE stripping its Mesa loader
 * environment. Patched Hyprland's main() drops ambient SYS_NICE before children
 * are launched; ordinary applications must not inherit it.
 */
#define _GNU_SOURCE
#include <linux/capability.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
/* The caller's loader environment is untrusted and stripped by AT_SECURE.
 * Read the same root-owned configuration as SDDM instead of pinning an old
 * Mesa prefix. Restoring that configuration also restores the loader choice.
 */
static int mesa_library_path(char path[PATH_MAX]) {
  const char *config = "/etc/environment.d/90-omarchy-j713-rendering.conf";
  const char key[] = "LD_LIBRARY_PATH=";
  struct stat st;
  char *line = NULL;
  size_t size = 0;
  int found = 0, valid = 0;
  int fd = open(config, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return -1;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
      (st.st_mode & 022) || st.st_size > 65536) {
    close(fd);
    return -1;
  }
  FILE *file = fdopen(fd, "r");
  if (!file) { close(fd); return -1; }
  while (getline(&line, &size, file) >= 0) {
    if (strncmp(line, key, sizeof(key)-1)) continue;
    char *value = line + sizeof(key)-1;
    size_t length = strcspn(value, "\r\n");
    if (++found != 1 || length >= PATH_MAX || length < 5) break;
    value[length] = 0;
    /* Root controls this file. Require the deliberately narrow single-prefix
     * format written by the package selector; no shell expansion or lists. */
    if (strncmp(value, "/opt/asahi-", 11) ||
        strcmp(value + length - 4, "/lib") ||
        strpbrk(value, " :\t\"'$") || strstr(value, "/../")) break;
    memcpy(path, value, length+1);
    valid = 1;
  }
  int error = ferror(file);
  free(line);
  fclose(file);
  return !error && found == 1 && valid ? 0 : -1;
}

int main(int argc, char **argv) {
  (void)argc;
  char mesa_path[PATH_MAX];
  char loader_path[PATH_MAX + sizeof("/opt/m4-hyprland/lib:")];
  if (mesa_library_path(mesa_path)) {
    fputs("Invalid root-owned Mesa selection\n", stderr);
    return 1;
  }
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
   * the fixed root-owned compositor backend plus the root-selected Mesa;
   * never accept a caller's loader path. Keep backend lifetime fixes across
   * Mesa prefix changes. */
  int length = snprintf(loader_path, sizeof(loader_path), "/opt/m4-hyprland/lib:%s", mesa_path);
  if (length < 0 || (size_t)length >= sizeof(loader_path))
    return 1;
  if (setenv("LD_LIBRARY_PATH", loader_path, 1))
    return 1;
  argv[0] = "/opt/m4-hyprland/bin/Hyprland";
  execv(argv[0], argv);
  perror("Hyprland");
  return 1;
}
