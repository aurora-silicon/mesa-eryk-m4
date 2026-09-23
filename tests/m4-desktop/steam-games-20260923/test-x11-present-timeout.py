#!/usr/bin/env python3
"""Exercise the actual X11 present-wait function with Mesa's real condition wait.

Only the GPU semaphore prerequisite and X11 chain storage are substituted. Runs
on the build host; no GPU needed. A baseline regression run must fail case2.
Usage: SOURCE_ROOT HOST_BUILD_DIR [--baseline-ref REF]
"""
import argparse,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('build',type=Path);p.add_argument('--baseline-ref');a=p.parse_args();a.source=a.source.resolve();a.build=a.build.resolve()
f='src/vulkan/wsi/wsi_common_x11.c'
s=subprocess.check_output(['git','-C',str(a.source),'show',f'{a.baseline_ref}:{f}'],text=True) if a.baseline_ref else (a.source/f).read_text()
start=s.index('static VkResult x11_wait_for_present(');end=s.index('\nstatic unsigned',start);fn=s[start:end]
pre='''#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "util/cnd_monotonic.h"
#include "util/timespec.h"
typedef int VkResult;
#define VK_SUCCESS 0
#define VK_TIMEOUT 2
#define VK_ERROR_DEVICE_LOST -4
struct wsi_swapchain { int unused; };
struct x11_swapchain { struct wsi_swapchain base; mtx_t present_progress_mutex; struct u_cnd_monotonic present_progress_cond; uint64_t present_id; VkResult present_progress_error; };
static VkResult semaphore_result=VK_SUCCESS;
static VkResult wsi_swapchain_wait_for_present_semaphore(const struct wsi_swapchain *c,uint64_t id,uint64_t timeout) { (void)c;(void)id;(void)timeout;return semaphore_result; }
'''
post='''
int main(void) {
 struct x11_swapchain c={0};
 mtx_init(&c.present_progress_mutex,mtx_plain);u_cnd_monotonic_init(&c.present_progress_cond);
 int errors=0;VkResult result;
 c.present_id=1;result=x11_wait_for_present(&c.base,1,1000000);
 printf("completed: %d expected0\\n",result);errors+=result!=VK_SUCCESS;
 c.present_id=0;result=x11_wait_for_present(&c.base,1,1000000);
 printf("condition timeout: %d expected2\\n",result);errors+=result!=VK_TIMEOUT;
 semaphore_result=VK_TIMEOUT;result=x11_wait_for_present(&c.base,1,1000000);
 printf("semaphore timeout: %d expected2\\n",result);errors+=result!=VK_TIMEOUT;
 semaphore_result=VK_ERROR_DEVICE_LOST;result=x11_wait_for_present(&c.base,1,1000000);
 printf("semaphore failure: %d expected-4\\n",result);errors+=result!=VK_ERROR_DEVICE_LOST;
 semaphore_result=VK_SUCCESS;c.present_id=1;c.present_progress_error=VK_ERROR_DEVICE_LOST;result=x11_wait_for_present(&c.base,1,1000000);
 printf("presentation failure: %d expected-4\\n",result);errors+=result!=VK_ERROR_DEVICE_LOST;
 u_cnd_monotonic_destroy(&c.present_progress_cond);mtx_destroy(&c.present_progress_mutex);
 return errors?1:0;
}
'''
# Reuse Meson's flags for cnd_monotonic.c so C11 thread ABI/config matches it.
commands=json.loads((a.build/'compile_commands.json').read_text());entry=next(c for c in commands if c['file'].endswith('/cnd_monotonic.c'))
import shlex
flags=shlex.split(entry['command']);idx=flags.index('-o');flags=flags[:idx]
clean=[];skip=False
for v in flags:
 if skip:skip=False;continue
 if v in ('-MQ','-MF'):skip=True;continue
 if v not in ('-MD','-MMD'):clean.append(v)
flags=clean
with tempfile.TemporaryDirectory(prefix='x11-present-test-') as td:
 src=Path(td)/'probe.c';exe=Path(td)/'probe';src.write_text(pre+fn+post)
 subprocess.run(flags+[str(src),str(a.build/'src/util/libmesa_util.a'),str(a.build/'src/c11/impl/libmesa_util_c11.a'),'-pthread','-o',str(exe)],cwd=a.build,check=True)
 raise SystemExit(subprocess.run([str(exe)]).returncode)
