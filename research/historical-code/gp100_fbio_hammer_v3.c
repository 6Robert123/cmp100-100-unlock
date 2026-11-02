#define _GNU_SOURCE
#include <errno.h>
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

static uint64_t ns_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

int main(int argc, char **argv) {
    char path[256];
    struct stat st;
    volatile uint32_t *bar;
    uint64_t start, deadline, ring_seen = 0, writes = 0;
    uint32_t max_fbpa = 0, max_fbp = 0, last_ctrl = 0xffffffffu;
    int fd;

    if (argc < 2) {
        fprintf(stderr, "usage: %s BDF [seconds]\n", argv[0]);
        return 2;
    }
    double seconds = argc >= 3 ? atof(argv[2]) : 12.0;
    snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/resource0", argv[1]);
    fd = open(path, O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) { perror("open resource0"); return 2; }
    if (fstat(fd, &st) || st.st_size < (off_t)BAR0_LEN) {
        fprintf(stderr, "BAR0 too small\n"); return 2;
    }
    bar = mmap(NULL, BAR0_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bar == MAP_FAILED) { perror("mmap"); return 2; }

    cpu_set_t cpus;
    CPU_ZERO(&cpus); CPU_SET(1, &cpus);
    (void)sched_setaffinity(0, sizeof cpus, &cpus);
    (void)mlockall(MCL_CURRENT | MCL_FUTURE);

    start = ns_now();
    deadline = start + (uint64_t)(seconds * 1000000000.0);
    puts("HAMMER_V3_ARMED"); fflush(stdout);

    while (ns_now() < deadline) {
        /* Hot path: no prerequisite read and no full fence/printf. */
        bar[CTRL_FBIO / 4] = 0;
        __asm__ __volatile__("" ::: "memory");
        writes++;

        /* Sample only once per 65536 writes; never touch gated FBPA windows. */
        if ((writes & 0xffffu) == 0) {
            uint32_t ctrl = bar[CTRL_FBIO / 4];
            uint32_t fa = bar[RING_FBPA_NR / 4];
            uint32_t fp = bar[RING_FBP_NR / 4];
            if (fa != 0xffffffffu && fa > max_fbpa) max_fbpa = fa;
            if (fp != 0xffffffffu && fp > max_fbp) max_fbp = fp;
            if (ctrl != last_ctrl) {
                printf("T_US=%" PRIu64 " CTRL=0x%08x FBPA_NR=0x%08x FBP_NR=0x%08x DEVINIT=0x%08x\n",
                       (ns_now()-start)/1000, ctrl, fa, fp, bar[DEVINIT/4]);
                fflush(stdout); last_ctrl = ctrl;
            }
            if (fa && fa != 0xffffffffu) {
                if (!ring_seen) ring_seen = ns_now();
                if (ns_now() - ring_seen > 300000000ull) break;
            }
        }
    }
    printf("HAMMER_V3_DONE writes=%" PRIu64 " elapsed_us=%" PRIu64
           " max_FBPA=%u max_FBP=%u final_CTRL=0x%08x DEVINIT=0x%08x\n",
           writes, (ns_now()-start)/1000, max_fbpa, max_fbp,
           bar[CTRL_FBIO/4], bar[DEVINIT/4]);
    return 0;
}
