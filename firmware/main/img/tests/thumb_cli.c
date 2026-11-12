// Host build of img_thumb for tests/run.sh: thumb_cli <src> <dst> <side>
#include <stdio.h>
#include <stdlib.h>
#include "img/thumb.h"

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: thumb_cli <src> <dst> <side>\n");
        return 2;
    }
    const char *why = img_thumb(argv[1], argv[2], atoi(argv[3]));
    printf("%s\n", why ? why : "ok");
    return why ? 1 : 0;
}
