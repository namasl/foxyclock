/* Streaming search for the app descriptor in a firmware upload (design §11).
 * On ESP8266 the descriptor is not at a fixed offset, so the stream is
 * scanned for its magic word at 4-byte-aligned image offsets, and candidate
 * matches are checked for the descriptor's shape (the magic also appears in
 * code as a literal). */
#ifndef XY_IMGSCAN_H
#define XY_IMGSCAN_H
#include <stddef.h>
#include <stdint.h>

#define XY_APP_DESC_MAGIC 0xABCD5432u
#define XY_APP_DESC_SIZE  256     /* sizeof(esp_app_desc_t) */

typedef struct {
    uint8_t win[XY_APP_DESC_SIZE + 4 + 1024];
    size_t win_len;       /* bytes held in win */
    size_t win_off;       /* image offset of win[0], always a multiple of 4 */
    int result;           /* 0 searching, 1 found ours, -1 found another project */
    char project[32], version[32];
} xy_imgscan_t;

void xy_imgscan_init(xy_imgscan_t *s);
/* Feed the next len (<= 1024) bytes of the image. Returns s->result. */
int xy_imgscan_feed(xy_imgscan_t *s, const uint8_t *data, size_t len, const char *want_project);

#endif
