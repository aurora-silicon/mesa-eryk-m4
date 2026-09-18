/* cc -O2 -pthread: CPU-only FEX lookup-cache/data-limit regression. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
static atomic_int done, ready;
static void *worker(void *arg) { atomic_fetch_add(&ready,1); while(!atomic_load(&done)) usleep(10000);return arg; }
int main(void) {
 setvbuf(stdout,NULL,_IONBF,0);alarm(15);
 struct rlimit limit={4ULL<<30,4ULL<<30};
 if(setrlimit(RLIMIT_DATA,&limit)) {perror("setrlimit");return 1;}
 pthread_t threads[32];int n;
 for(n=0;n<32;n++) { int e=pthread_create(&threads[n],NULL,worker,NULL); if(e) {printf("CREATE_FAIL n=%d error=%s\n",n,strerror(e));break;} }
 atomic_store(&done,1);for(int i=0;i<n;i++) pthread_join(threads[i],NULL);
 FILE *f=fopen("/proc/self/status","r"); char line[256]; if(f){while(fgets(line,sizeof(line),f))if(!strncmp(line,"VmData:",7)||!strncmp(line,"VmRSS:",6))fputs(line,stdout);fclose(f);}
 void *large=mmap(NULL,5ULL<<30,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 if(large!=MAP_FAILED||errno!=ENOMEM) {puts("LIMIT_NOT_ENFORCED");return 2;}
 if(n!=32)return 3;
 puts("PASS: 32 threads under 4 GiB; excessive guest allocation still rejected");return 0;
}
