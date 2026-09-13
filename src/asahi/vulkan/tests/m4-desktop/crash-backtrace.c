/* SPDX-License-Identifier: MIT
 * Diagnostic-only native userspace crash stack for targets where ptrace fails.
 * cc -shared -fPIC -O2 crash-backtrace.c -o crash-backtrace.so
 * Use LD_PRELOAD=/path/crash-backtrace.so inside the mandatory hardware guard.
 * backtrace() is not async-signal-safe: keep the external timeout and watchdog.
 */
#define _GNU_SOURCE
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
static void crash(int sig)
{
   void *frames[48];
   const char marker[] = "DIAGNOSTIC_CRASH_BACKTRACE\n";
   (void)!write(STDERR_FILENO, marker, sizeof(marker) - 1);
   int n = backtrace(frames, 48);
   backtrace_symbols_fd(frames, n, STDERR_FILENO);
   /* SA_RESETHAND restores the default; deliver after leaving this handler. */
   raise(sig);
}
__attribute__((constructor)) static void setup(void)
{
   void *warm[1];
   backtrace(warm, 1);
   struct sigaction action = {.sa_handler = crash, .sa_flags = SA_RESETHAND};
   sigemptyset(&action.sa_mask);
   sigaction(SIGSEGV, &action, 0);
   sigaction(SIGABRT, &action, 0);
}
