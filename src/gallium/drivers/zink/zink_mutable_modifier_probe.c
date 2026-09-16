/* SPDX-License-Identifier: MIT
 * Manual hardware regression. Run only under the watchdog/thermal guard.
 * GALLIUM_THREAD=0 exposes the underlying context for the internal surface API.
 */
#include <stdint.h>
#include "zink_public.h"
#include "zink_surface.h"
#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "util/u_inlines.h"
#include "drm-uapi/drm_fourcc.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL: %s line %d\n", #x, __LINE__); exit(1); } } while (0)

int main(int argc, char **argv)
{
   CHECK(argc == 2);
   CHECK(getenv("GALLIUM_THREAD") && !strcmp(getenv("GALLIUM_THREAD"), "0"));
   setvbuf(stdout, NULL, _IOLBF, 0);
   uint64_t modifier = strtoull(argv[1], NULL, 0);
   int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
   CHECK(fd >= 0);
   struct pipe_screen *screen = zink_drm_create_screen(fd, NULL, NULL);
   CHECK(screen);
   struct pipe_context *ctx = screen->context_create(screen, NULL, 0);
   CHECK(ctx);
   for (unsigned reverse = 0; reverse < 2; reverse++) {
      struct pipe_resource templ = {
         .target = PIPE_TEXTURE_2D,
         .format = reverse ? PIPE_FORMAT_R8G8B8A8_UNORM : PIPE_FORMAT_B8G8R8A8_UNORM,
         .width0 = 16, .height0 = 16, .depth0 = 1, .array_size = 1,
         .bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SHARED,
      };
      struct pipe_resource *res = screen->resource_create_with_modifiers(screen, &templ, &modifier, 1);
      CHECK(res);
      struct pipe_surface surf = {
         .texture = res,
         .format = reverse ? PIPE_FORMAT_B8G8R8A8_UNORM : PIPE_FORMAT_R8G8B8A8_UNORM,
      };
      /* This is the exact allocation and view path in the Xwayland core. */
      CHECK(zink_create_fb_surface(ctx, &surf));
      union pipe_color_union color = { .f = {191.0f / 255, 64.0f / 255, 16.0f / 255, 1} };
      ctx->clear_render_target(ctx, &surf, &color, 0, 0, 16, 16, false);
      struct pipe_box box = {.width = 16, .height = 16, .depth = 1};
      struct pipe_transfer *transfer = NULL;
      const unsigned char *map = ctx->texture_map(ctx, res, 0, PIPE_MAP_READ, &box, &transfer);
      CHECK(map && transfer);
      unsigned bad = 0;
      unsigned char expected[] = {reverse ? 16 : 191, 64, reverse ? 191 : 16, 255};
      for (unsigned y = 0; y < 16; y++)
         for (unsigned x = 0; x < 16; x++)
            for (unsigned c = 0; c < 4; c++)
               bad += abs((int)map[y * transfer->stride + x * 4 + c] - expected[c]) > 1;
      printf("reverse=%u modifier=%llx bad=%u first=%u,%u,%u,%u\n", reverse,
             (unsigned long long)modifier, bad, map[0], map[1], map[2], map[3]);
      ctx->texture_unmap(ctx, transfer);
      CHECK(!bad);
      pipe_resource_reference(&res, NULL);
   }
   ctx->destroy(ctx);
   screen->destroy(screen);
   close(fd);
   puts("PASS: mutable modifier views and 512 pixels");
   return 0;
}
