/* GP100 CFG1 race — host-side variant for a passthrough VM (v4).
 *
 * Based on unlock/historical/gp100_cfg1_race_optfbio.c from
 * github.com/6Robert123/cmp100-100-unlock (MIT, (c) 6Robert123), rev 235e399.
 * Register logic (addresses, trigger, target, plant_active, LMR window) is unchanged.
 *
 * Host-safety changes (a host hung with an NMI on 09.11.2026 while the original tight
 * loop polled BAR0 across a vfio reset at VM start):
 *  - reset gate before EVERY BAR0 access, via sysfs config space (always safe):
 *    card COMMAND (kernel blocks user config reads during a reset; memory decode bit),
 *    upstream bridge Secondary Bus Reset bit, upstream Data Link Layer Link Active;
 *  - --quiet S: for the first S seconds do not touch BAR0 at all, only log config-space
 *    changes (the firmware/OVMF phase resets and reprograms the card);
 *  - BAR0 is polled every --period-us (default 200 us) instead of a tight loop, which cuts
 *    the time an MMIO read is in flight by orders of magnitude;
 *  - 0xffffffff and a closed gate mean "wait", not "trigger" and not "abort";
 *  - --wait-reset: trigger only after CFG1 was seen non-stock (post-reset), so a stale
 *    stock value from a previous session is never acted on.
 *
 * usage: gp100_cfg1_race_host BDF TARGET_HEX TIMEOUT_S [--wait-reset] [--quiet S] [--period-us N]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <sched.h>
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

static void sleep_us(unsigned us)
{
	struct timespec ts = { us / 1000000u, (long)(us % 1000000u) * 1000L };
	nanosleep(&ts, NULL);
}

/* ---- reset gate ---------------------------------------------------------------------- */
static int cfg_fd = -1, up_fd = -1, up_dll_cap = 0;
static unsigned up_pcie = 0;

static int cfg_rd(int f, unsigned off, void *buf, size_t n)
{
	return pread(f, buf, n, off) == (ssize_t)n ? 0 : -1;
}

static unsigned find_pcie_cap(int f)
{
	uint8_t pos = 0, id = 0, guard = 0;
	if (cfg_rd(f, 0x34, &pos, 1))
		return 0;
	while (pos && guard++ < 48) {
		if (cfg_rd(f, pos, &id, 1))
			return 0;
		if (id == 0x10)
			return pos;
		if (cfg_rd(f, pos + 1u, &pos, 1))
			return 0;
	}
	return 0;
}

static int gate_init(const char *bdf, char *upname, size_t upsz)
{
	char p[PATH_MAX + 32], real[PATH_MAX], *slash;
	uint32_t lnkcap = 0;
	snprintf(p, sizeof p, "/sys/bus/pci/devices/%s/config", bdf);
	cfg_fd = open(p, O_RDONLY | O_CLOEXEC);
	if (cfg_fd < 0)
		return -1;
	snprintf(p, sizeof p, "/sys/bus/pci/devices/%s", bdf);
	if (!realpath(p, real) || !(slash = strrchr(real, '/')))
		return -1;
	*slash = 0;
	slash = strrchr(real, '/');
	snprintf(upname, upsz, "%s", slash ? slash + 1 : "?");
	snprintf(p, sizeof p, "%s/config", real);
	up_fd = open(p, O_RDONLY | O_CLOEXEC);
	if (up_fd < 0)
		return -1;
	up_pcie = find_pcie_cap(up_fd);
	if (up_pcie && !cfg_rd(up_fd, up_pcie + 0x0c, &lnkcap, 4))
		up_dll_cap = (lnkcap >> 20) & 1;
	return 0;
}

struct gate {
	uint16_t cmd, bctl, lnksta;
	int open;
};

static struct gate gate_read(void)
{
	struct gate g = { 0xffff, 0xffff, 0, 0 };
	if (cfg_rd(cfg_fd, 0x04, &g.cmd, 2))
		g.cmd = 0xffff;
	if (cfg_rd(up_fd, 0x3e, &g.bctl, 2))
		g.bctl = 0xffff;
	if (up_dll_cap && cfg_rd(up_fd, up_pcie + 0x12, &g.lnksta, 2))
		g.lnksta = 0;
	g.open = g.cmd != 0xffff && (g.cmd & 0x2) && g.bctl != 0xffff && !(g.bctl & 0x40) &&
		 (!up_dll_cap || (g.lnksta & 0x2000));
	return g;
}

static void gate_log(uint64_t start, struct gate g, struct gate *last)
{
	if (g.cmd == last->cmd && g.bctl == last->bctl && g.open == last->open &&
	    (!up_dll_cap || ((g.lnksta ^ last->lnksta) & 0x2000) == 0))
		return;
	printf("CFG_US=%" PRIu64 " COMMAND=0x%04x BRIDGECTL=0x%04x DLLLA=%d gate=%s\n",
	       (ns_now() - start) / 1000, g.cmd, g.bctl, up_dll_cap ? !!(g.lnksta & 0x2000) : -1,
	       g.open ? "open" : "CLOSED");
	fflush(stdout);
	*last = g;
}

/* ---- original plant logic, gated ----------------------------------------------------- */
static void plant_active(volatile uint32_t *bar, uint32_t opt, uint32_t target,
			 int only_stock, unsigned *rewrites)
{
	for (unsigned i = 0; i < 16; i++) {
		if ((opt >> i) & 1u)
			continue; /* floorswept */
		if (!gate_read().open)
			return;
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

static void usage(const char *a)
{
	fprintf(stderr, "usage: %s BDF TARGET_HEX TIMEOUT_S [--wait-reset] [--quiet S] [--period-us N]\n", a);
}

int main(int argc, char **argv)
{
	char path[256], *end, upname[64];
	unsigned long parsed, timeout_s, quiet_s = 0, period_us = 200;
	uint32_t target, opt, v, last_seen = 0x12345678u;
	int fd, wait_reset = 0, seen_nonstock;
	struct stat st;
	volatile uint32_t *bar;
	uint64_t start, hit, deadline, quiet_until;
	cpu_set_t cpus;
	struct gate g, glast = { 0, 0, 0, -1 };

	if (argc < 4) {
		usage(argv[0]);
		return 2;
	}
	for (int i = 4; i < argc; i++) {
		if (!strcmp(argv[i], "--wait-reset"))
			wait_reset = 1;
		else if (!strcmp(argv[i], "--quiet") && i + 1 < argc)
			quiet_s = strtoul(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--period-us") && i + 1 < argc)
			period_us = strtoul(argv[++i], NULL, 0);
		else {
			usage(argv[0]);
			return 2;
		}
	}
	errno = 0;
	parsed = strtoul(argv[2], &end, 0);
	if (errno || *end || parsed > UINT32_MAX) {
		fprintf(stderr, "invalid target: %s\n", argv[2]);
		return 2;
	}
	target = (uint32_t)parsed;
	timeout_s = strtoul(argv[3], &end, 0);
	if (*end || timeout_s == 0 || timeout_s > 3600 || quiet_s >= timeout_s || period_us < 20 ||
	    period_us > 5000) {
		fprintf(stderr, "invalid timeout/quiet/period (timeout 1..3600 s, quiet < timeout, period 20..5000 us)\n");
		return 2;
	}
	if (gate_init(argv[1], upname, sizeof upname)) {
		fprintf(stderr, "cannot open config space of the card or its upstream bridge: %s\n", strerror(errno));
		return 2;
	}
	g = gate_read();
	if (!quiet_s && !g.open) {
		printf("REFUSE gate closed at arm and no --quiet phase; arm only while the VM runs\n");
		return 4;
	}
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

	CPU_ZERO(&cpus);
	CPU_SET(0, &cpus);
	(void)sched_setaffinity(0, sizeof(cpus), &cpus);
	(void)mlockall(MCL_CURRENT | MCL_FUTURE);

	seen_nonstock = !wait_reset;
	printf("ARMED trigger=0x%08x target=0x%08x timeout_s=%lu quiet_s=%lu period_us=%lu wait_reset=%d "
	       "gate: upstream=%s pcie_cap=0x%02x dll_active_reporting=%d\n",
	       STOCK_CFG1, target, timeout_s, quiet_s, period_us, wait_reset, upname, up_pcie, up_dll_cap);
	fflush(stdout);

	start = ns_now();
	quiet_until = start + (uint64_t)quiet_s * 1000000000ull;
	deadline = start + (uint64_t)timeout_s * 1000000000ull;
	while (ns_now() < deadline) {
		g = gate_read();
		gate_log(start, g, &glast);
		if (ns_now() < quiet_until) {
			sleep_us(1000); /* config space only during the firmware phase */
			continue;
		}
		if (!g.open) {
			sleep_us((unsigned)period_us);
			continue;
		}
		v = bar[CFG1_ADDR / 4];
		if (v == STOCK_CFG1 && seen_nonstock)
			goto do_hit; /* no printing before the write */
		if (v != last_seen) {
			printf("SEEN_US=%" PRIu64 " CFG1=0x%08x\n", (ns_now() - start) / 1000, v);
			fflush(stdout);
			last_seen = v;
		}
		if (v != STOCK_CFG1 && v != 0xffffffffu)
			seen_nonstock = 1;
		sleep_us((unsigned)period_us);
		continue;
do_hit:
		{
			uint32_t last_cfg = 0, last_cs = 0, last_lmr = 0, last_di = 0;
			uint64_t observe_until;
			unsigned rewrites = 0;
			hit = ns_now();
			opt = bar[OPT_FBIO_ADDR / 4];
			plant_active(bar, opt, target, 0, NULL);
			while (ns_now() - hit < 50000000ull) {
				if (!gate_read().open)
					break;
				if (bar[LMR_ADDR / 4] != 0)
					break;
				plant_active(bar, opt, target, 1, &rewrites);
			}
			if (gate_read().open) {
				printf("HIT_NS=%" PRIu64 " DELTA_US=%" PRIu64
				       " READBACK=0x%08x CSTATUS=0x%08x LMR=0x%08x DEVINIT=0x%08x REWRITES=%u OPT_FBIO=0x%08x\n",
				       hit, (hit - start) / 1000, bar[CFG1_ADDR / 4], bar[CSTATUS_ADDR / 4],
				       bar[LMR_ADDR / 4], bar[DEVINIT_ADDR / 4], rewrites, opt);
			} else {
				printf("HIT_NS=%" PRIu64 " DELTA_US=%" PRIu64 " gate closed right after the write\n",
				       hit, (hit - start) / 1000);
			}
			fflush(stdout);
			observe_until = hit + 2000000000ull;
			while (ns_now() < observe_until) {
				g = gate_read();
				gate_log(start, g, &glast);
				if (!g.open) {
					sleep_us(1000);
					continue;
				}
				uint32_t cfg = bar[CFG1_ADDR / 4], cs = bar[CSTATUS_ADDR / 4];
				uint32_t lmr = bar[LMR_ADDR / 4], di = bar[DEVINIT_ADDR / 4];
				if (cfg != last_cfg || cs != last_cs || lmr != last_lmr || di != last_di) {
					printf("OBSERVE_US=%" PRIu64 " CFG1=0x%08x CSTATUS=0x%08x LMR=0x%08x DEVINIT=0x%08x\n",
					       (ns_now() - hit) / 1000, cfg, cs, lmr, di);
					fflush(stdout);
					last_cfg = cfg, last_cs = cs, last_lmr = lmr, last_di = di;
				}
				sleep_us((unsigned)period_us);
			}
			if (gate_read().open) {
				printf("FINAL CFG1=0x%08x CSTATUS=0x%08x LMR=0x%08x DEVINIT=0x%08x\n",
				       bar[CFG1_ADDR / 4], bar[CSTATUS_ADDR / 4], bar[LMR_ADDR / 4], bar[DEVINIT_ADDR / 4]);
				for (unsigned i = 0; i < 16; i++) {
					if ((opt >> i) & 1u)
						continue;
					if (!gate_read().open)
						break;
					printf("FINAL_FBPA%02u CFG1=0x%08x CSTATUS=0x%08x\n", i,
					       bar[(FBPA_BASE(i) + 0x204u) / 4], bar[(FBPA_BASE(i) + 0x20cu) / 4]);
				}
			} else {
				printf("FINAL skipped: gate closed\n");
			}
			munmap((void *)bar, BAR0_LEN);
			close(fd);
			return 0;
		}
	}
	printf("TIMEOUT last_seen_CFG1=0x%08x seen_nonstock=%d\n", last_seen, seen_nonstock);
	munmap((void *)bar, BAR0_LEN);
	close(fd);
	return 3;
}
