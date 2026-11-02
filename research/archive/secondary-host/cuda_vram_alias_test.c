#define _GNU_SOURCE
#include <dlfcn.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int CUresult;
typedef int CUdevice;
typedef void *CUcontext;
typedef uint64_t CUdeviceptr;

#define LOAD(name) do { *(void **)(&name) = dlsym(lib, #name); if (!(name)) { \
    fprintf(stderr, "missing CUDA symbol %s\n", #name); return 2; } } while (0)
#define CUDA(call) do { CUresult r_ = (call); if (r_) { \
    fprintf(stderr, "%s failed: %d\n", #call, r_); return 3; } } while (0)

int main(int argc, char **argv) {
    void *lib = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
    CUresult (*cuInit)(unsigned);
    CUresult (*cuDeviceGet)(CUdevice *, int);
    CUresult (*cuCtxCreate_v2)(CUcontext *, unsigned, CUdevice);
    CUresult (*cuCtxDestroy_v2)(CUcontext);
    CUresult (*cuDeviceTotalMem_v2)(size_t *, CUdevice);
    CUresult (*cuMemGetInfo_v2)(size_t *, size_t *);
    CUresult (*cuMemAlloc_v2)(CUdeviceptr *, size_t);
    CUresult (*cuMemFree_v2)(CUdeviceptr);
    CUresult (*cuMemsetD32_v2)(CUdeviceptr, unsigned, size_t);
    CUresult (*cuMemcpyDtoH_v2)(void *, CUdeviceptr, size_t);
    CUdevice dev;
    CUcontext ctx;
    CUdeviceptr ptr;
    size_t total, free_b, alloc_b, seg = 256ull << 20;
    uint32_t *host;
    unsigned segments, i, pass, failures = 0;
    int quick = argc == 2 && !strcmp(argv[1], "--quick");

    if (!lib) { fprintf(stderr, "dlopen libcuda: %s\n", dlerror()); return 2; }
    LOAD(cuInit); LOAD(cuDeviceGet); LOAD(cuCtxCreate_v2); LOAD(cuCtxDestroy_v2);
    LOAD(cuDeviceTotalMem_v2); LOAD(cuMemGetInfo_v2); LOAD(cuMemAlloc_v2);
    LOAD(cuMemFree_v2); LOAD(cuMemsetD32_v2); LOAD(cuMemcpyDtoH_v2);
    CUDA(cuInit(0)); CUDA(cuDeviceGet(&dev, 0)); CUDA(cuCtxCreate_v2(&ctx, 0, dev));
    CUDA(cuDeviceTotalMem_v2(&total, dev)); CUDA(cuMemGetInfo_v2(&free_b, &alloc_b));
    printf("CUDA_TOTAL_MIB=%zu CUDA_FREE_MIB=%zu\n", total >> 20, free_b >> 20);
    if (free_b <= (768ull << 20)) { fprintf(stderr, "not enough free VRAM\n"); return 4; }
    alloc_b = (free_b - (512ull << 20)) / seg * seg;
    CUDA(cuMemAlloc_v2(&ptr, alloc_b));
    segments = (unsigned)(alloc_b / seg);
    printf("ALLOC_MIB=%zu SEGMENTS=%u SEGMENT_MIB=256\n", alloc_b >> 20, segments);
    if (quick) {
        for (i = 0; i < segments; i++) {
            uint32_t pattern = 0xc1000000u ^ (i * 0x01010101u);
            CUDA(cuMemsetD32_v2(ptr + (uint64_t)i * seg, pattern, seg / 4));
        }
        for (i = 0; i < segments; i++) {
            uint32_t got[2], want = 0xc1000000u ^ (i * 0x01010101u);
            CUDA(cuMemcpyDtoH_v2(&got[0], ptr + (uint64_t)i * seg, 4));
            CUDA(cuMemcpyDtoH_v2(&got[1], ptr + (uint64_t)(i + 1) * seg - 4, 4));
            if (got[0] != want || got[1] != want) {
                printf("MISMATCH segment=%u want=0x%08x first=0x%08x last=0x%08x\n",
                       i, want, got[0], got[1]);
                failures++;
            }
        }
        CUDA(cuMemFree_v2(ptr)); CUDA(cuCtxDestroy_v2(ctx));
        printf("RESULT=%s FAILURES=%u TESTED_MIB=%zu\n",
               failures ? "ALIAS_OR_CORRUPTION" : "PASS_QUICK", failures, alloc_b >> 20);
        dlclose(lib);
        return failures ? 5 : 0;
    }
    host = malloc(seg);
    if (!host) { fprintf(stderr, "host allocation failed\n"); return 4; }
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < segments; i++) {
            uint32_t pattern = (pass ? 0x3effffffu : 0xc1000000u) ^ (i * 0x01010101u);
            CUDA(cuMemsetD32_v2(ptr + (uint64_t)i * seg, pattern, seg / 4));
        }
        for (i = 0; i < segments; i++) {
            uint32_t want = (pass ? 0x3effffffu : 0xc1000000u) ^ (i * 0x01010101u);
            size_t word;
            CUDA(cuMemcpyDtoH_v2(host, ptr + (uint64_t)i * seg, seg));
            for (word = 0; word < seg / 4; word++) {
                if (host[word] != want) {
                    if (failures < 32)
                        printf("MISMATCH pass=%u segment=%u byte=%zu want=0x%08x got=0x%08x\n",
                               pass, i, word * 4, want, host[word]);
                    failures++;
                }
            }
            printf("PASS_PROGRESS pass=%u segment=%u/%u failures=%u\n",
                   pass + 1, i + 1, segments, failures);
            fflush(stdout);
        }
    }
    free(host);
    CUDA(cuMemFree_v2(ptr)); CUDA(cuCtxDestroy_v2(ctx));
    printf("RESULT=%s FAILURES=%u TESTED_MIB=%zu\n",
           failures ? "ALIAS_OR_CORRUPTION" : "PASS_FULL", failures, alloc_b >> 20);
    dlclose(lib);
    return failures ? 5 : 0;
}
