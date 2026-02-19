/* stubs.c — provides symbols referenced by the hashcash library that are
 * normally defined in hashcash.c (the CLI main). Since we use the library
 * directly without hashcash.c, we supply these stubs ourselves.
 *
 * Also provides AltiVec minter stubs: the libfastmint.c minter table
 * references AltiVec routines by name regardless of platform. On non-PPC
 * hardware the _test() functions return 0 (= not capable), so the actual
 * minter functions are never invoked — but the linker still needs the
 * symbols to exist.
 */

#include <stdio.h>
#include <stdlib.h>

/* ── die_msg ─────────────────────────────────────────────────── */
/* Called by sdb.c and array.c on unrecoverable internal errors. */
void die_msg(const char *msg) {
    fprintf(stderr, "Fatal: %s\n", msg ? msg : "(unknown error)");
    exit(1);
}

/* ── AltiVec capability stubs ────────────────────────────────── */
/* _test() returns 0 → libfastmint treats the core as unavailable
 * and never calls the corresponding minter function.             */

typedef unsigned int uInt32;
typedef int (*hashcash_callback_t)(int, int, int,
                                   double, double, void *);

#define ALTIVEC_STUB_TEST(name)         \
    int name##_test(void) { return 0; }

#define ALTIVEC_STUB_MINTER(name)                                    \
    unsigned long name(int bits, int *best,                          \
        unsigned char *block, const uInt32 IV[5],                    \
        int tailIndex, unsigned long maxIter,                        \
        hashcash_callback_t cb, void *user_args,                     \
        double counter, double expected) {                           \
        (void)bits; (void)best; (void)block; (void)IV;              \
        (void)tailIndex; (void)maxIter; (void)cb; (void)user_args;  \
        (void)counter; (void)expected;                               \
        return 0;                                                    \
    }

ALTIVEC_STUB_TEST(minter_altivec_standard_1)
ALTIVEC_STUB_TEST(minter_altivec_compact_2)
ALTIVEC_STUB_TEST(minter_altivec_standard_2)

ALTIVEC_STUB_MINTER(minter_altivec_standard_1)
ALTIVEC_STUB_MINTER(minter_altivec_compact_2)
ALTIVEC_STUB_MINTER(minter_altivec_standard_2)
