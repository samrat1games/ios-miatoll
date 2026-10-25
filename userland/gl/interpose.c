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
