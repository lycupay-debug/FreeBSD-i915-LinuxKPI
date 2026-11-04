#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libdrm/drm.h>
#include <libdrm/i915_drm.h>

#define TEST_SIZE (4ULL * 1024 * 1024)
#define PAGE_BYTES 4096ULL
#define DROP_CACHES_PATH "/mnt/lindebugfs/dri/drmn0/i915_gem_drop_caches"

static uint64_t pattern(size_t index)
{
    return 0x9e3779b97f4a7c15ULL ^ ((uint64_t)index * 0xd6e8feb86659fd93ULL);
}

int main(void)
{
    int fd, cachefd;
    struct drm_i915_gem_create create = { .size = TEST_SIZE };
    struct drm_i915_gem_mmap_offset offset;
    struct drm_i915_gem_madvise advice;
    volatile uint64_t *map;
    size_t words = TEST_SIZE / sizeof(uint64_t), i;
    size_t bad = 0;

    fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open renderD128"); return 2; }
    if (ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create) != 0) {
        perror("GEM_CREATE"); return 2;
    }
    advice.handle = create.handle;
    advice.madv = I915_MADV_WILLNEED;
    advice.retained = 0;
    if (ioctl(fd, DRM_IOCTL_I915_GEM_MADVISE, &advice) != 0 || !advice.retained) {
        perror("GEM_MADVISE_WILLNEED"); return 2;
    }
    memset(&offset, 0, sizeof(offset));
    offset.handle = create.handle;
    offset.flags = I915_MMAP_OFFSET_WB;
    if (ioctl(fd, DRM_IOCTL_I915_GEM_MMAP_OFFSET, &offset) != 0) {
        perror("GEM_MMAP_OFFSET_WB"); return 2;
    }
    map = mmap(NULL, TEST_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)offset.offset);
    if (map == MAP_FAILED) { perror("mmap WB"); return 2; }

    for (i = 0; i < words; i++) map[i] = pattern(i);
    for (i = 0; i < words; i++) if (map[i] != pattern(i)) { bad++; break; }
    if (bad) { fprintf(stderr, "FAIL: initial write/read mismatch\n"); return 1; }
    printf("INITIAL_DATA_OK size=%llu handle=%u offset=0x%llx\n",
        (unsigned long long)TEST_SIZE, create.handle,
        (unsigned long long)offset.offset);
    fflush(stdout);

    cachefd = open(DROP_CACHES_PATH, O_WRONLY | O_CLOEXEC);
    if (cachefd < 0) { perror("open i915_gem_drop_caches"); return 2; }
    if (write(cachefd, "1\n", 3) != 3) { perror("write DROP_UNBOUND"); return 2; }
    close(cachefd);
    printf("DROP_UNBOUND_TRIGGERED\n");
    fflush(stdout);

    advice.handle = create.handle;
    advice.madv = I915_MADV_WILLNEED;
    advice.retained = 0;
    if (ioctl(fd, DRM_IOCTL_I915_GEM_MADVISE, &advice) != 0 || !advice.retained) {
        fprintf(stderr, "FAIL: object no longer retained after shrink\n");
        return 2;
    }
    printf("WILLNEED_RETAINED_AFTER_SHRINK=1\n");

    for (i = 0; i < words; i++) {
        if (map[i] != pattern(i)) {
            fprintf(stderr, "DATA_LOSS index=%zu expected=0x%llx got=0x%llx\n",
                i, (unsigned long long)pattern(i), (unsigned long long)map[i]);
            bad++;
            break;
        }
    }
    if (bad) {
        printf("RESULT=DATA_LOSS\n");
        return 1;
    }
    printf("RESULT=DATA_PRESERVED\n");
    munmap((void *)map, TEST_SIZE);
    close(fd);
    return 0;
}
