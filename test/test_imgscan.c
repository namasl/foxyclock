#include <stdlib.h>
#include "minitest.h"
#include "xy_imgscan.h"

static uint8_t *make_image(size_t size, size_t desc_at, const char *project)
{
    uint8_t *img = calloc(1, size);
    img[0] = 0xE9;
    for (size_t i = 1; i < size; i++) img[i] = (uint8_t)(i * 7);
    uint32_t m = XY_APP_DESC_MAGIC;
    memcpy(img + desc_at, &m, 4);
    memset(img + desc_at + 4, 0, XY_APP_DESC_SIZE - 4);
    strcpy((char *)img + desc_at + 16, "v1.2");
    strcpy((char *)img + desc_at + 48, project);
    return img;
}

static int feed_all(const uint8_t *img, size_t size, size_t chunk, xy_imgscan_t *s)
{
    xy_imgscan_init(s);
    for (size_t off = 0; off < size; off += chunk) {
        size_t n = size - off < chunk ? size - off : chunk;
        int r = xy_imgscan_feed(s, img + off, n, "xyclock");
        if (r) return r;
    }
    return 0;
}

static void test_imgscan_finds_desc_across_chunk_boundary(void)
{
    /* Descriptor straddles the 1024-byte chunk boundary at many offsets. */
    for (size_t at = 900; at <= 1100; at += 4) {
        uint8_t *img = make_image(5000, at, "xyclock");
        xy_imgscan_t s;
        ASSERT_EQ_INT(feed_all(img, 5000, 1024, &s), 1);
        ASSERT_EQ_STR(s.version, "v1.2");
        free(img);
    }
}

static void test_imgscan_odd_chunk_sizes(void)
{
    uint8_t *img = make_image(8000, 3000, "xyclock");
    size_t sizes[] = { 1, 3, 7, 100, 333, 1023 };
    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        xy_imgscan_t s;
        ASSERT_EQ_INT(feed_all(img, 8000, sizes[k], &s), 1);
    }
    free(img);
}

static void test_imgscan_other_project_rejected(void)
{
    uint8_t *img = make_image(4000, 1200, "otherthing");
    xy_imgscan_t s;
    ASSERT_EQ_INT(feed_all(img, 4000, 1024, &s), -1);
    ASSERT_EQ_STR(s.project, "otherthing");
    free(img);
}

static void test_imgscan_unaligned_magic_ignored(void)
{
    uint8_t *img = make_image(4000, 1202, "xyclock");   /* not 4-aligned: not a descriptor */
    xy_imgscan_t s;
    ASSERT_EQ_INT(feed_all(img, 4000, 1024, &s), 0);
    free(img);
}

static void test_imgscan_skips_magic_used_as_constant(void)
{
    /* Code containing the magic as a literal before the real descriptor. */
    uint8_t *img = make_image(6000, 4000, "xyclock");
    uint32_t m = XY_APP_DESC_MAGIC;
    memcpy(img + 400, &m, 4);                 /* followed by non-zero bytes */
    xy_imgscan_t s;
    ASSERT_EQ_INT(feed_all(img, 6000, 1024, &s), 1);
    ASSERT_EQ_STR(s.project, "xyclock");
    free(img);
}

static void test_imgscan_none(void)
{
    uint8_t *img = make_image(4000, 0, "x");
    memset(img, 0x11, 4000);
    xy_imgscan_t s;
    ASSERT_EQ_INT(feed_all(img, 4000, 1024, &s), 0);
    free(img);
}

static void test_imgscan_real_build(void)
{
    FILE *f = fopen("../build/xyclock.bin", "rb");
    if (!f) { printf("  (skipped: no ../build/xyclock.bin)\n"); return; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    uint8_t *img = malloc((size_t)n);
    ASSERT_EQ_INT(fread(img, 1, (size_t)n, f), n);
    fclose(f);
    xy_imgscan_t s;
    ASSERT_EQ_INT(feed_all(img, (size_t)n, 1024, &s), 1);
    ASSERT_EQ_STR(s.project, "xyclock");
    free(img);
}

int main(void)
{
    RUN(test_imgscan_finds_desc_across_chunk_boundary);
    RUN(test_imgscan_odd_chunk_sizes);
    RUN(test_imgscan_other_project_rejected);
    RUN(test_imgscan_unaligned_magic_ignored);
    RUN(test_imgscan_skips_magic_used_as_constant);
    RUN(test_imgscan_none);
    RUN(test_imgscan_real_build);
    return TEST_SUMMARY("test_imgscan");
}
