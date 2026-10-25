/*
 * ios-miatoll: CoreAnimation's OpenGL ES renderer asks every layer's IOSurface for its bulk attachments
 * (CA::Render::iosurface_get_edr_factor -> IOSurfaceClientGetBulkAttachments, a client: shared page at +0x70). IOSurface.framework copies them from the surface's shared
 * page (*(surface + 0x70) + 0x30); for some surfaces of our kernel's IOSurface that pointer is NULL and
 * the window server crashes there. Such a surface has no attachments: say so, and CoreAnimation takes
 * its default (EDR factor 1). Interposed by dyld (0146 allows it) for every image of backboardd.
 */
#include <IOSurface/IOSurfaceRef.h>
#include <stdint.h>
#include <unistd.h>

extern int IOSurfaceClientGetBulkAttachments(void *surface, void *attachments, size_t *size);

static int
my_IOSurfaceClientGetBulkAttachments(void *surface, void *attachments, size_t *size)
{
	static int said;

	if (surface == NULL || *(void *const *)((const uint8_t *)surface + 0x70) == NULL) {
		if (said++ < 4) {
			const char line[] = "redwood-gl: an IOSurface without its shared page: no bulk attachments\n";
			write(2, line, sizeof(line) - 1);
		}
		return 0xe00002bc;      /* kIOReturnError */
	}
	return IOSurfaceClientGetBulkAttachments(surface, attachments, size);
}

__attribute__((used, section("__DATA,__interpose"))) static const struct {
	const void *replacement, *replacee;
} interposers[] = {
	{ (const void *)my_IOSurfaceClientGetBulkAttachments, (const void *)IOSurfaceClientGetBulkAttachments },
};

/*
 * ios-miatoll: what backboardd was doing when it crashed (SIGSEGV/SIGBUS): the registers, and for
 * a pointer that looks like an IOSurfaceClient its surface ID (+0x80) and shared page (+0x70).
 * Said to stderr (the kernel copies backboardd's), then the default action as before.
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ucontext.h>
#include <malloc/malloc.h>
#include <objc/runtime.h>

static void
crashed(int sig, siginfo_t *info, void *context)
{
	ucontext_t *uc = context;
	char line[600];
	int n = 0;

	n += snprintf(line + n, sizeof(line) - n, "redwood-gl: CRASH sig %d addr %p pc %#llx lr %#llx", sig, info->si_addr,
	    (unsigned long long)uc->uc_mcontext->__ss.__pc, (unsigned long long)uc->uc_mcontext->__ss.__lr);
	for (int i = 0; i < 12 && n < (int)sizeof(line) - 40; i++) {
		n += snprintf(line + n, sizeof(line) - n, " x%d=%#llx", i, (unsigned long long)uc->uc_mcontext->__ss.__x[i]);
	}
	n += snprintf(line + n, sizeof(line) - n, " x19=%#llx x20=%#llx x21=%#llx\n",
	    (unsigned long long)uc->uc_mcontext->__ss.__x[19], (unsigned long long)uc->uc_mcontext->__ss.__x[20],
	    (unsigned long long)uc->uc_mcontext->__ss.__x[21]);
	write(2, line, (size_t)n);
	/* which of the saved registers is an IOSurface, and what it is */
	for (int i = 19; i <= 28; i++) {
		void *p = (void *)uc->uc_mcontext->__ss.__x[i];

		if (p == NULL || malloc_zone_from_ptr(p) == NULL) {
			continue;
		}
		const char *cls = object_getClassName((id)p);
		if (cls == NULL || strstr(cls, "IOSurface") == NULL) {
			continue;
		}
		n = snprintf(line, sizeof(line), "redwood-gl: CRASH x%d is a %s: id %u %zux%zu fmt %#x planes %zu alloc %zu client %p\n",
		    i, cls, IOSurfaceGetID((IOSurfaceRef)p), IOSurfaceGetWidth((IOSurfaceRef)p), IOSurfaceGetHeight((IOSurfaceRef)p),
		    IOSurfaceGetPixelFormat((IOSurfaceRef)p), IOSurfaceGetPlaneCount((IOSurfaceRef)p),
		    IOSurfaceGetAllocSize((IOSurfaceRef)p), *(void **)((uint8_t *)p + 0x8));
		write(2, line, (size_t)n);
	}
	signal(sig, SIG_DFL);
}

__attribute__((constructor)) static void
crash_handler(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = crashed;
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
}
