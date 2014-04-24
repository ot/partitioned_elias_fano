#define BOOST_TEST_MODULE strict_elias_fano

#include "test_generic_sequence.hpp"

#include "strict_elias_fano.hpp"
#include <vector>
#include <cstdlib>

BOOST_AUTO_TEST_CASE(strict_elias_fano)
{
    quasi_succinct::global_parameters params;

    uint64_t n = 10000;
    uint64_t universe = uint64_t(2 * n);
    auto seq = random_sequence(universe, n, true);

    test_sequence(quasi_succinct::strict_elias_fano(), params, universe, seq);
}
