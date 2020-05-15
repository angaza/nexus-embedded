#include "src/nexus_security.h"
#include "unity.h"
#include "utils/siphash_24.h"

// Other support libraries
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/********************************************************
 * DEFINITIONS
 *******************************************************/
/********************************************************
 * PRIVATE TYPES
 *******************************************************/

/********************************************************
 * PRIVATE DATA
 *******************************************************/

/********************************************************
 * PRIVATE FUNCTIONS
 *******************************************************/

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
}

void test_nexus_security__memclr_size_too_large__limits_size(void)
{
    uint8_t test_vector[100];
    for (uint8_t i = 0; i < 100; i++)
    {
        test_vector[i] = 0xfa;
    }
    // if limiting works, we won't get an out of bounds access error
    (void) nexus_secure_memclr(
        &test_vector[0], sizeof(test_vector), sizeof(test_vector) + 100000);
    TEST_ASSERT_EACH_EQUAL_UINT8(0, test_vector, 100);
}
