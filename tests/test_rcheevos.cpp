// Sanity checks that the vendored rcheevos builds and links correctly.

#include <doctest/doctest.h>
#include <rc_error.h>
#include <rc_runtime_types.h>

TEST_CASE("rcheevos accepts valid achievement logic") {
    // "8-bit value at address 0x1234 equals 5"; a positive result is the memory needed to hold
    // the parsed trigger.
    CHECK(rc_trigger_size("0xH1234=5") > 0);
}

TEST_CASE("rcheevos rejects malformed achievement logic") {
    // Comparison without a right-hand side; errors are negative RC_* codes from rc_error.h.
    CHECK(rc_trigger_size("0xH1234=") < 0);
}
