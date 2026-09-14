/* SPDX-License-Identifier: MIT
 * Bounded statistical CPU-PC sampling without perf/kernel dependencies.
 * LD_PRELOAD this library with M4_CPU_PROFILE_DIR and M4_CPU_PROFILE_EXE
 * (executable basename). Run GPU workloads only inside gpu-experiment.py.
 * Build: cc -O2 -shared -fPIC cpu-sample.c -o cpu-sample.so -ldl
 * Output: profile-PID.tsv, module-relative PCs for addr2line; 100 Hz CPU time.
 * SIGPROF/ITIMER_PROF must not already belong to the application.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

enum { CAPACITY = 40000 };
static uintptr_t pcs[CAPACITY];
static _Atomic unsigned count;
static const char *directory;
static int enabled;

static void sample(int sig, siginfo_t *info, void *context)
{
   (void)sig;
   (void)info;
   unsigned index = atomic_fetch_add_explicit(&count, 1, memory_order_relaxed);
   if (index >= CAPACITY)
      return;
   ucontext_t *uc = context;
#if defined(__aarch64__)
   pcs[index] = uc->uc_mcontext.pc;
#elif defined(__x86_64__)
   pcs[index] = uc->uc_mcontext.gregs[REG_RIP];
#else
#error Unsupported PC register
#endif
}

__attribute__((constructor)) static void start(void)
{
   directory = getenv("M4_CPU_PROFILE_DIR");
   const char *wanted = getenv("M4_CPU_PROFILE_EXE");
   char exe[4096];
   ssize_t size = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
   if (!directory || !wanted || size < 0)
      return;
   exe[size] = 0;
   const char *base = strrchr(exe, '/');
   if (strcmp(base ? base + 1 : exe, wanted))
      return;
   struct sigaction old, action = {.sa_sigaction = sample, .sa_flags = SA_SIGINFO | SA_RESTART};
   struct itimerval prior, timer = {{0, 10000}, {0, 10000}};
   if (sigaction(SIGPROF, NULL, &old) || getitimer(ITIMER_PROF, &prior) ||
       old.sa_handler != SIG_DFL || prior.it_value.tv_sec || prior.it_value.tv_usec)
      return;
   sigemptyset(&action.sa_mask);
   if (sigaction(SIGPROF, &action, NULL) == 0 && setitimer(ITIMER_PROF, &timer, NULL) == 0)
      enabled = 1;
}

__attribute__((destructor)) static void finish(void)
{
   if (!enabled)
      return;
   struct itimerval stopped = {{0, 0}, {0, 0}};
   setitimer(ITIMER_PROF, &stopped, NULL);
   unsigned samples = atomic_load_explicit(&count, memory_order_relaxed);
   char path[4096];
   snprintf(path, sizeof(path), "%s/profile-%d.tsv", directory, getpid());
   FILE *out = fopen(path, "w");
   if (!out)
      return;
   fprintf(out, "# CPU samples=%u retained=%u interval_us=10000\nmodule\toffset\tsymbol\n",
           samples, samples < CAPACITY ? samples : CAPACITY);
   for (unsigned i = 0; i < samples && i < CAPACITY; ++i) {
      Dl_info info = {0};
      dladdr((void *)pcs[i], &info);
      fprintf(out, "%s\t%#lx\t%s\n", info.dli_fname ? info.dli_fname : "unknown",
              (unsigned long)(pcs[i] - (uintptr_t)info.dli_fbase),
              info.dli_sname ? info.dli_sname : "unknown");
   }
   fclose(out);
}
