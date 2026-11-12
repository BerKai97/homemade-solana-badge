// kb: type into whichever on-screen keyboard is open (a Lua app's ui.input,
// Wi-Fi's password, the Store's search), as if its keys were pressed.
//   kb                  is one open? (prints "kb: open" or "kb: closed")
//   kb type <words...>  type them, one space between
//   kb hex <hex>        type these bytes exactly (any characters: what tools send)
//   kb del [n]          erase n characters (1)
//   kb ok | kb cancel   finish it
// The console runs on its own task; the edit runs on the OS task (os_post).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/os.h"
#include "core/os_cmd.h"
#include "core/ui.h"
#include "kb/kb_cmd.h"

#define TEXT_MAX    200

typedef struct {
    int erase;                  // characters to erase first
    int finish;                 // 0, 1 = OK, 2 = cancel
    char text[];
} edit_t;

static void apply(void *arg)
{
    edit_t *e = arg;
    bool open = e->finish ? ui_kb_finish(e->finish == 1) : ui_kb_edit(e->text, e->erase);
    if (!open) {
        printf("kb: no keyboard is open\n");
    }
    free(e);
}

static int post(const char *text, int erase, int finish)
{
    size_t n = text ? strlen(text) : 0;
    edit_t *e = malloc(sizeof(*e) + n + 1);
    if (!e) {
        return 1;
    }
    e->erase = erase;
    e->finish = finish;
    memcpy(e->text, text ? text : "", n + 1);
    if (!os_post(apply, e)) {
        free(e);
        printf("kb: busy, try again\n");
        return 1;
    }
    return 0;
}

static int hexval(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static int cmd_kb(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "";
    char text[TEXT_MAX + 1];
    if (!sub[0]) {
        printf("kb: %s\n", ui_kb_is_open() ? "open" : "closed");    // a read: no lock needed for a flag
        return 0;
    }
    if (!strcmp(sub, "type") && argc > 2) {
        text[0] = 0;
        for (int i = 2; i < argc; i++) {
            if (i > 2) {
                strlcat(text, " ", sizeof(text));
            }
            strlcat(text, argv[i], sizeof(text));
        }
        return post(text, 0, 0);
    }
    if (!strcmp(sub, "hex") && argc == 3) {
        size_t n = strlen(argv[2]);
        if (n % 2 || n / 2 > TEXT_MAX) {
            printf("kb hex: an even number of hex digits, up to %d bytes\n", TEXT_MAX);
            return 1;
        }
        for (size_t i = 0; i < n / 2; i++) {
            int hi = hexval(argv[2][2 * i]), lo = hexval(argv[2][2 * i + 1]);
            if (hi < 0 || lo < 0) {
                printf("kb hex: not hex\n");
                return 1;
            }
            text[i] = (char)(hi << 4 | lo);
        }
        text[n / 2] = 0;
        return post(text, 0, 0);
    }
    if (!strcmp(sub, "del")) {
        int n = argc > 2 ? atoi(argv[2]) : 1;
        return post(NULL, n > 0 ? n : 1, 0);
    }
    if (!strcmp(sub, "ok") || !strcmp(sub, "cancel")) {
        return post(NULL, 0, !strcmp(sub, "ok") ? 1 : 2);
    }
    printf("kb [type <words> | hex <hex> | del [n] | ok | cancel]\n");
    return 1;
}

void kb_cmd_init(void)
{
    os_cmd_register("kb", "kb [type <words>|hex <hex>|del [n]|ok|cancel]  type into the on-screen keyboard", cmd_kb);
}

#ifdef SOLANA_OS_EMULATOR
#include <emscripten.h>
// For the emulator page: route the PC keyboard to the badge's keyboard while one is open.
EMSCRIPTEN_KEEPALIVE int emu_kb_open(void)
{
    return ui_kb_is_open();
}
#endif
