#include "sk.h"
#include <assert.h>
#include <stdio.h>
void sk_preview_init(void);
int sk_preview_select(uint16_t id);
const uint8_t *sk_preview_render(int32_t pos, float sub, uint32_t ms, uint32_t flash);
int main(void) {
    sk_preview_init();
    assert(sk_preview_select(1));
    const uint8_t *pixels = sk_preview_render(10, 0, 100, 0);
    assert(pixels[3] == 0);
    assert(pixels[(120 * 240 + 120) * 4 + 3] == 255);
    unsigned colored = 0;
    for (int i = 0; i < 240 * 240; i++)
        if (pixels[i * 4] > 30 || pixels[i * 4 + 1] > 30)
            colored++;
    assert(colored > 1000);
    assert(sk_preview_select(6));
    assert(!sk_preview_select(99));
    pixels = sk_preview_render(1, 0, 200, 0);
    FILE *f = fopen("preview.ppm", "wb");
    assert(f);
    fprintf(f, "P6\n240 240\n255\n");
    for (int i = 0; i < 240 * 240; i++)
        fwrite(pixels + i * 4, 1, 3, f);
    fclose(f);
    puts("LVGL: all widgets rendered, circular alpha mask verified");
    return 0;
}
