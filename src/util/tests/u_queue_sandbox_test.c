/* SPDX-License-Identifier: MIT */
/* A sandbox can allow sched_setscheduler only for pid zero. Verify that
 * minimum-priority workers set their own policy and still execute jobs. */
#include "util/u_queue.h"
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static void
check_worker(void *job, void *unused, int thread_index)
{
   (void)unused;
   (void)thread_index;
   *(int *)job = sched_getscheduler(0) == SCHED_BATCH &&
                getpriority(PRIO_PROCESS, 0) == 19;
}

static int
sandbox_child(void)
{
   const struct rlimit no_core = {0, 0};
   setrlimit(RLIMIT_CORE, &no_core);
   struct sock_filter filter[] = {
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_sched_setscheduler, 0, 3),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 1, 0),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
   };
   struct sock_fprog program = {sizeof(filter) / sizeof(filter[0]), filter};
   if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
       prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program))
      return 77;
   struct util_queue queue = {0};
   struct util_queue_fence fence;
   int ran = 0;
   util_queue_fence_init(&fence);
   if (!util_queue_init(&queue, "sandbox", 4, 2,
                        UTIL_QUEUE_INIT_USE_MINIMUM_PRIORITY, NULL))
      return 1;
   util_queue_add_job(&queue, &ran, &fence, check_worker, NULL, 0);
   util_queue_fence_wait(&fence);
   util_queue_destroy(&queue);
   util_queue_fence_destroy(&fence);
   return ran ? 0 : 1;
}

int
main(void)
{
   pid_t child = fork();
   if (child < 0)
      return 1;
   if (!child)
      _exit(sandbox_child());
   int status;
   if (waitpid(child, &status, 0) != child)
      return 1;
   if (!WIFEXITED(status)) {
      fprintf(stderr, "queue sandbox child killed by signal %d\n", WTERMSIG(status));
      return 1;
   }
   return WEXITSTATUS(status);
}
