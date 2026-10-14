#include "sk.h"
#include <stdio.h>
static sk_project project;
static uint8_t bytes[SK_MAX_BUNDLE + 1];
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    size_t len = fread(bytes, 1, sizeof(bytes), f);
    fclose(f);
    if (!sk_project_decode(bytes, len, &project)) {
        fprintf(stderr, "Invalid bundle\n");
        return 1;
    }
    printf("C decoder accepted %u views, start %u\n", project.view_count, project.start_view);
    for (int i = 0; i < project.view_count; i++)
        printf("%u %s mode=%u LEDs=%u widgets=%u\n", project.views[i].id, project.views[i].name,
               project.views[i].haptic.mode, project.views[i].leds.mode,
               project.views[i].widget_count);
    return 0;
}
