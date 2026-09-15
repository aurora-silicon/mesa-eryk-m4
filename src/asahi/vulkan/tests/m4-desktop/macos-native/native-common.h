// SPDX-License-Identifier: MIT
#pragma once
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static double now(void)
{
    struct timespec t;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static void stage(NSString *directory, NSString *ready, NSString *release)
{
    if (!directory) return;
    NSError *error = nil;
    NSString *message = [NSString stringWithFormat:@"pid=%d monotonic=%.9f\n", getpid(), now()];
    CHECK([message writeToFile:[directory stringByAppendingPathComponent:ready]
                   atomically:YES encoding:NSUTF8StringEncoding error:&error]);
    const char *path = [directory stringByAppendingPathComponent:release].fileSystemRepresentation;
    double end = now() + 180;
    while (access(path, F_OK)) {
        CHECK(errno == ENOENT && now() < end);
        usleep(10000);
    }
}
