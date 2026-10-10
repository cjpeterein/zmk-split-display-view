/* Host check: cc -I src tests/rotate_test.c -o /tmp/rotate_test && /tmp/rotate_test */
#include <assert.h>
#include <string.h>
#include "rotate.h"

int main(void) {
    /* 2 wide, 3 tall: a b / c d / e f */
    const uint8_t src[] = {'a', 'b', 'c', 'd', 'e', 'f'};
    uint8_t dst[6];

    sdv_rotate(src, dst, 2, 3, 0, 3, false);
    assert(memcmp(dst, "ecafdb", 6) == 0);

    sdv_rotate(src, dst, 2, 3, 0, 3, true);
    assert(memcmp(dst, "bdface", 6) == 0);

    memset(dst, '.', 6);
    sdv_rotate(src, dst, 2, 3, 1, 2, false);
    assert(memcmp(dst, ".c..d.", 6) == 0);
    return 0;
}
