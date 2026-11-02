#define _GNU_SOURCE
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BAR0_LEN     0x1000000u
#define CTRL_FBIO    0x021938u
#define RING_FBPA_NR 0x12006cu
#define RING_FBP_NR  0x120074u
#define DEVINIT      0x02240cu
#define BAD          0xbadf5040u

static uint64_t ns_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return (uint64_t)t.tv_sec*1000000000ull + (uint64_t)t.tv_nsec;
}
static int live(uint32_t v) { return v != 0xffffffffu && v != BAD; }

int main(int argc, char **argv) {
    char path[256]; struct stat st; volatile uint32_t *bar; int fd;
    uint64_t start, deadline, writes=0, live_reads=0, dead_reads=0;
    uint64_t first_nonzero_ns=0, first_zero_after_ns=0, ring_seen_ns=0;
    uint32_t max_fbpa=0, max_fbp=0, seen_nonzero=0;
    double seconds = argc >= 3 ? atof(argv[2]) : 12.0;
    if (argc < 2) { fprintf(stderr,"usage: %s BDF [seconds]\n",argv[0]); return 2; }
    snprintf(path,sizeof path,"/sys/bus/pci/devices/%s/resource0",argv[1]);
    fd=open(path,O_RDWR|O_SYNC|O_CLOEXEC); if(fd<0){perror("open");return 2;}
    if(fstat(fd,&st)||st.st_size<(off_t)BAR0_LEN){fputs("BAR0 too small\n",stderr);return 2;}
    bar=mmap(NULL,BAR0_LEN,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(bar==MAP_FAILED){perror("mmap");return 2;}
    cpu_set_t c; CPU_ZERO(&c); CPU_SET(1,&c); (void)sched_setaffinity(0,sizeof c,&c);
    (void)mlockall(MCL_CURRENT|MCL_FUTURE);
    start=ns_now(); deadline=start+(uint64_t)(seconds*1e9);
    puts("INTERCEPT_V4_ARMED"); fflush(stdout);

    while(ns_now()<deadline){
        uint32_t ctrl=bar[CTRL_FBIO/4];
        if(!live(ctrl)){ dead_reads++; continue; }
        live_reads++;

        /* First instruction after the liveness read is the gate override. */
        if(ctrl!=0 && !first_nonzero_ns){ first_nonzero_ns=ns_now(); seen_nonzero=ctrl; }
        bar[CTRL_FBIO/4]=0;
        __asm__ __volatile__("" ::: "memory");
        writes++;
        if(first_nonzero_ns && !first_zero_after_ns && bar[CTRL_FBIO/4]==0)
            first_zero_after_ns=ns_now();

        if((writes & 0x3fffu)==0){
            uint32_t fa=bar[RING_FBPA_NR/4], fp=bar[RING_FBP_NR/4];
            if(live(fa)&&fa>max_fbpa)max_fbpa=fa;
            if(live(fp)&&fp>max_fbp)max_fbp=fp;
            if(live(fa)&&fa){
                if(!ring_seen_ns)ring_seen_ns=ns_now();
                if(ns_now()-ring_seen_ns>300000000ull)break;
            }
        }
    }
    printf("INTERCEPT_V4_DONE elapsed_us=%" PRIu64 " live=%" PRIu64 " dead=%" PRIu64
           " writes=%" PRIu64 " seen_nonzero=0x%08x intercept_ns=%" PRIu64
           " max_FBPA=%u max_FBP=%u final_CTRL=0x%08x DEVINIT=0x%08x\n",
           (ns_now()-start)/1000,live_reads,dead_reads,writes,seen_nonzero,
           first_zero_after_ns&&first_nonzero_ns ? first_zero_after_ns-first_nonzero_ns : 0,
           max_fbpa,max_fbp,bar[CTRL_FBIO/4],bar[DEVINIT/4]);
    return 0;
}
