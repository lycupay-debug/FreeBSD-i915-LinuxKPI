CC?=cc
CFLAGS?=-O2 -pipe
CFLAGS+=-Wall -Wextra -Werror
DRM_CFLAGS!=pkg-config --cflags libdrm
DRM_LIBS!=pkg-config --libs libdrm

.PHONY: all clean
all: gem_wb_shmem_roundtrip

gem_wb_shmem_roundtrip: gem_wb_shmem_roundtrip.c
	${CC} ${DRM_CFLAGS} ${CFLAGS} -o ${.TARGET} ${.IMPSRC} ${LDFLAGS} ${DRM_LIBS}

clean:
	rm -f gem_wb_shmem_roundtrip
