#include <stdio.h>
#include <stdint.h>
#include <time.h>
int main(){for(int i=0;i<5;i++){struct timespec a,b;uint64_t t,f;clock_gettime(CLOCK_MONOTONIC,&a);__asm__ volatile("isb; mrs %0, cntvct_el0; mrs %1, cntfrq_el0":"=r"(t),"=r"(f));clock_gettime(CLOCK_MONOTONIC,&b);printf("%llu %llu %.9f %.9f offset=%.9f\n",(unsigned long long)t,(unsigned long long)f,a.tv_sec+a.tv_nsec/1e9,b.tv_sec+b.tv_nsec/1e9,(double)t/f-(a.tv_sec+a.tv_nsec/1e9));}}
