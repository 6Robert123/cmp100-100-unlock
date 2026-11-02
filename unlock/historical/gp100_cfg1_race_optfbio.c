/* GP100 CFG1 race — OPT_FBIO-aware.
 * Old helper hardcoded FBPA 4..15 (matches OPT_FBIO=0x0f).
 * New CMP has OPT_FBIO=0xf000 → active FBPA 0..11. Writing only 4..15
 * leaves 0..3 at stock and breaks a coherent 12GB profile.
 *
 * usage: gp100_cfg1_race_optfbio 0000:01:00.0 0x02669000
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#ifdef __linux__
#include <sched.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BAR0_LEN 0x1000000u
#define CFG1_ADDR 0x9a0204u
#define CSTATUS_ADDR 0x9a020cu
#define LMR_ADDR 0x100ce0u
#define DEVINIT_ADDR 0x2240cu
#define OPT_FBIO_ADDR 0x21c14u
#define STOCK_CFG1 0x02559000u
#define FBPA_BASE(i) (0x900000u + (uint32_t)(i) * 0x4000u)

static uint64_t ns_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void plant_active(volatile uint32_t *bar, uint32_t opt, uint32_t target,
			 int only_stock, unsigned *rewrites)
{
	for (unsigned i = 0; i < 16; i++) {
		if ((opt >> i) & 1u)
			continue; /* floorswept */
		volatile uint32_t *cfg = &bar[(FBPA_BASE(i) + 0x204u) / 4];
		uint32_t cur = *cfg;
		if (only_stock && cur != STOCK_CFG1)
			continue;
		if (cur != target) {
			*cfg = target;
			if (rewrites)
				(*rewrites)++;
		}
	}
	__sync_synchronize();
}

int main(int argc, char **argv)
{
	char path[256], *end;
	unsigned long parsed;
	uint32_t target, opt;
	int fd;
	struct stat st;
	volatile uint32_t *bar;
	uint64_t start, hit, deadline;
#ifdef __linux__
	cpu_set_t cpus;
#endif

	if (argc != 3) {
		fprintf(stderr, "usage: %s 0000:01:00.0 TARGET_CFG1_HEX\n", argv[0]);
		return 2;
	}
	errno = 0;
	parsed = strtoul(argv[2], &end, 0);
	if (errno || *end || parsed > UINT32_MAX) {
		fprintf(stderr, "invalid target: %s\n", argv[2]);
		return 2;
	}
	target = (uint32_t)parsed;
	snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/resource0", argv[1]);
	fd = open(path, O_RDWR | O_SYNC | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open: %s\n", strerror(errno));
		return 2;
	}
	if (fstat(fd, &st) || st.st_size < BAR0_LEN) {
		fprintf(stderr, "BAR0 unavailable\n");
		close(fd);
		return 2;
	}
	bar = mmap(NULL, BAR0_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (bar == MAP_FAILED) {
		fprintf(stderr, "mmap: %s\n", strerror(errno));
		close(fd);
		return 2;
	}

#ifdef __linux__
	CPU_ZERO(&cpus);
	CPU_SET(0, &cpus);
	(void)sched_setaffinity(0, sizeof(cpus), &cpus);
#endif
	(void)mlockall(MCL_CURRENT | MCL_FUTURE);

	opt = bar[OPT_FBIO_ADDR / 4];
	printf("ARMED trigger=0x%08x target=0x%08x OPT_FBIO=0x%08x active=",
	       STOCK_CFG1, target, opt);
	for (unsigned i = 0; i < 16; i++)
		if (!((opt >> i) & 1u))
			printf("%u%s", i, i == 15 ? "" : ",");
	printf("\n");
	fflush(stdout);

	start = ns_now();
	deadline = start + 15000000000ull;
	while (ns_now() < deadline) {
		uint32_t v = bar[CFG1_ADDR / 4];
		if (v == STOCK_CFG1) {
			uint32_t last_cfg, last_cs, last_lmr, last_di;
			uint64_t observe_until;
			unsigned rewrites = 0;
			hit = ns_now();
			/* Re-read OPT_FBIO at hit in case it was not stable at arm. */
			opt = bar[OPT_FBIO_ADDR / 4];
			plant_active(bar, opt, target, 0, NULL);
			/* Until LMR commits, keep replacing stock on active FBPAs. */
			while (bar[LMR_ADDR / 4] == 0 && ns_now() - hit < 50000000ull) {
				plant_active(bar, opt, target, 1, &rewrites);
			}
			printf("HIT_NS=%" PRIu64 " DELTA_US=%" PRIu64
			       " READBACK=0x%08x CSTATUS=0x%08x LMR=0x%08x "
			       "DEVINIT=0x%08x REWRITES=%u OPT_FBIO=0x%08x\n",
			       hit, (hit - start) / 1000, bar[CFG1_ADDR / 4],
			       bar[CSTATUS_ADDR / 4], bar[LMR_ADDR / 4],
			       bar[DEVINIT_ADDR / 4], rewrites, opt);
			fflush(stdout);
			last_cfg = bar[CFG1_ADDR / 4];
			last_cs = bar[CSTATUS_ADDR / 4];
			last_lmr = bar[LMR_ADDR / 4];
			last_di = bar[DEVINIT_ADDR / 4];
			observe_until = hit + 2000000000ull;
			while (ns_now() < observe_until) {
				uint32_t cfg = bar[CFG1_ADDR / 4];
				uint32_t cs = bar[CSTATUS_ADDR / 4];
				uint32_t lmr = bar[LMR_ADDR / 4];
				uint32_t di = bar[DEVINIT_ADDR / 4];
				if (cfg != last_cfg || cs != last_cs || lmr != last_lmr ||
				    di != last_di) {
					printf("OBSERVE_US=%" PRIu64
					       " CFG1=0x%08x CSTATUS=0x%08x "
					       "LMR=0x%08x DEVINIT=0x%08x\n",
					       (ns_now() - hit) / 1000, cfg, cs, lmr, di);
					fflush(stdout);
					last_cfg = cfg;
					last_cs = cs;
					last_lmr = lmr;
					last_di = di;
				}
			}
			printf("FINAL CFG1=0x%08x CSTATUS=0x%08x LMR=0x%08x "
			       "DEVINIT=0x%08x\n",
			       bar[CFG1_ADDR / 4], bar[CSTATUS_ADDR / 4],
			       bar[LMR_ADDR / 4], bar[DEVINIT_ADDR / 4]);
			for (unsigned i = 0; i < 16; i++) {
				if ((opt >> i) & 1u)
					continue;
				printf("FINAL_FBPA%02u CFG1=0x%08x CSTATUS=0x%08x\n", i,
				       bar[(FBPA_BASE(i) + 0x204u) / 4],
				       bar[(FBPA_BASE(i) + 0x20cu) / 4]);
			}
			munmap((void *)bar, BAR0_LEN);
			close(fd);
			return 0;
		}
	}
	printf("TIMEOUT CFG1=0x%08x LMR=0x%08x DEVINIT=0x%08x OPT_FBIO=0x%08x\n",
	       bar[CFG1_ADDR / 4], bar[LMR_ADDR / 4], bar[DEVINIT_ADDR / 4], opt);
	munmap((void *)bar, BAR0_LEN);
	close(fd);
	return 3;
}
