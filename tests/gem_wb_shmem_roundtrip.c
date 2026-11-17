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
#define DROP_CACHES_DEFAULT "/mnt/lindebugfs/dri/drmn0/i915_gem_drop_caches"

static uint64_t pattern(size_t index)
{
    return 0x9e3779b97f4a7c15ULL ^ ((uint64_t)index * 0xd6e8feb86659fd93ULL);
}

int main(void)
{
    int fd = -1, cachefd = -1, result = 2;
    struct drm_i915_gem_create create = { .size = TEST_SIZE };
    struct drm_i915_gem_mmap_offset offset;
    struct drm_i915_gem_madvise advice;
    struct drm_gem_close close_arg = { 0 };
    volatile uint64_t *map = MAP_FAILED;
    const char *drop_path = getenv("DROP_CACHES_NODE");
    const char drop_command[] = "1\n";
    size_t words = TEST_SIZE / sizeof(uint64_t), i;

    if (drop_path == NULL || *drop_path == '\0')
        drop_path = DROP_CACHES_DEFAULT;

    fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open renderD128"); goto out; }
    if (ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create) != 0) {
        perror("GEM_CREATE"); goto out;
    }

    advice.handle = create.handle;
    advice.madv = I915_MADV_WILLNEED;
    advice.retained = 0;
    if (ioctl(fd, DRM_IOCTL_I915_GEM_MADVISE, &advice) != 0 || !advice.retained) {
        perror("GEM_MADVISE_WILLNEED"); goto out;
    }

    memset(&offset, 0, sizeof(offset));
    offset.handle = create.handle;
    offset.flags = I915_MMAP_OFFSET_WB;
    if (ioctl(fd, DRM_IOCTL_I915_GEM_MMAP_OFFSET, &offset) != 0) {
        perror("GEM_MMAP_OFFSET_WB"); goto out;
    }

    map = mmap(NULL, TEST_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
        (off_t)offset.offset);
    if (map == MAP_FAILED) { perror("mmap WB"); goto out; }

    for (i = 0; i < words; i++)
        map[i] = pattern(i);
    for (i = 0; i < words; i++) {
        if (map[i] != pattern(i)) {
            fprintf(stderr, "FAIL: initial mismatch index=%zu\n", i);
            goto out;
        }
    }
    printf("INITIAL_DATA_OK size=%llu handle=%u offset=0x%llx\n",
        (unsigned long long)TEST_SIZE, create.handle,
        (unsigned long long)offset.offset);
    fflush(stdout);

    cachefd = open(drop_path, O_WRONLY | O_CLOEXEC);
    if (cachefd < 0) { perror("open i915_gem_drop_caches"); goto out; }
    if (write(cachefd, drop_command, sizeof(drop_command) - 1) !=
        (ssize_t)(sizeof(drop_command) - 1)) {
        perror("write DROP_UNBOUND"); goto out;
    }
    close(cachefd);
    cachefd = -1;
    printf("DROP_UNBOUND_TRIGGERED\n");
    fflush(stdout);

    advice.handle = create.handle;
    advice.madv = I915_MADV_WILLNEED;
    advice.retained = 0;
    if (ioctl(fd, DRM_IOCTL_I915_GEM_MADVISE, &advice) != 0 || !advice.retained) {
        fprintf(stderr, "FAIL: object no longer retained after shrink\n");
        goto out;
    }
    printf("WILLNEED_RETAINED_AFTER_SHRINK=1\n");

    for (i = 0; i < words; i++) {
        if (map[i] != pattern(i)) {
            fprintf(stderr, "DATA_LOSS index=%zu expected=0x%llx got=0x%llx\n",
                i, (unsigned long long)pattern(i), (unsigned long long)map[i]);
            printf("RESULT=DATA_LOSS\n");
            result = 1;
            goto out;
        }
    }
    printf("RESULT=DATA_PRESERVED\n");
    result = 0;

out:
    if (cachefd >= 0)
        close(cachefd);
    if (map != MAP_FAILED)
        munmap((void *)map, TEST_SIZE);
    if (fd >= 0) {
        if (create.handle != 0) {
            close_arg.handle = create.handle;
            (void)ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_arg);
        }
        close(fd);
    }
    return result;
}
