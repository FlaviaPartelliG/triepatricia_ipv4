/* test_ipv4.c - unit tests for the IPv4 primitives. */
#include "ipv4.h"
#include "test_util.h"

#include <string.h>

int main(void) {
    TEST_BEGIN("ipv4");

    uint32_t a;
    CHECK(ipv4_parse("192.168.1.1", &a));
    CHECK(a == 0xC0A80101u);
    CHECK(!ipv4_parse("256.0.0.1", &a));
    CHECK(!ipv4_parse("1.2.3", &a));
    CHECK(!ipv4_parse("1.2.3.4.5", &a));
    CHECK(!ipv4_parse("1.2.3.4x", &a));

    char buf[32];
    CHECK(strcmp(ipv4_format(0xC0A80101u, buf, sizeof(buf)), "192.168.1.1") == 0);

    /* masks */
    CHECK(ipv4_mask(0) == 0x00000000u);
    CHECK(ipv4_mask(1) == 0x80000000u);
    CHECK(ipv4_mask(24) == 0xFFFFFF00u);
    CHECK(ipv4_mask(32) == 0xFFFFFFFFu);

    /* bit_at: MSB is bit 0 */
    CHECK(ipv4_bit_at(0x80000000u, 0) == 1);
    CHECK(ipv4_bit_at(0x80000000u, 1) == 0);
    CHECK(ipv4_bit_at(0x00000001u, 31) == 1);

    /* common prefix length */
    CHECK(ipv4_common_prefix(0xFFFFFFFFu, 0xFFFFFFFFu) == 32);
    CHECK(ipv4_common_prefix(0x00000000u, 0x80000000u) == 0);
    CHECK(ipv4_common_prefix(0xC0A80100u, 0xC0A80200u) == 22);

    /* prefix parse normalizes host bits */
    prefix_t p;
    CHECK(prefix_parse("10.1.2.3/8", &p));
    CHECK(p.len == 8);
    CHECK(p.addr == 0x0A000000u);
    CHECK(prefix_parse("0.0.0.0/0", &p) && p.len == 0 && p.addr == 0);
    CHECK(prefix_parse("8.8.8.8", &p) && p.len == 32 && p.addr == 0x08080808u);
    CHECK(!prefix_parse("1.2.3.4/33", &p));
    CHECK(!prefix_parse("1.2.3.4/-1", &p));
    CHECK(!prefix_parse("1.2.3.4/x", &p));

    CHECK(strcmp(prefix_format(p, buf, sizeof(buf)), "8.8.8.8/32") == 0);

    /* matching */
    prefix_parse("192.168.0.0/16", &p);
    CHECK(prefix_matches(p, 0xC0A8FFFFu));
    CHECK(!prefix_matches(p, 0xC0A90000u));

    TEST_REPORT();
}
