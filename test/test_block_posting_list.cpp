#define BOOST_TEST_MODULE block_posting_list

#include "test_generic_sequence.hpp"

#include "block_posting_list.hpp"
#include "block_codecs.hpp"

#include <vector>
#include <cstdlib>
#include <algorithm>

template <typename BlockCodec>
void test_block_posting_list()
{
    typedef quasi_succinct::block_posting_list<BlockCodec> posting_list_type;
    uint64_t universe = 20000;
    for (size_t t = 0; t < 20; ++t) {
        double avg_gap = 1.1 + double(rand()) / RAND_MAX * 10;
        uint64_t n = uint64_t(universe / avg_gap);
        std::vector<uint64_t> docs = random_sequence(universe, n, true);
        std::vector<uint64_t> freqs(n);
        std::generate(freqs.begin(), freqs.end(),
                      []() { return (rand() % 256) + 1; });

        std::vector<uint8_t> data;
        posting_list_type::write(data, n, docs.begin(), freqs.begin());

        typename posting_list_type::document_enumerator e(data.data(), universe);
        BOOST_REQUIRE_EQUAL(n, e.size());
        for (size_t i = 0; i < n; ++i, e.next()) {
            MY_REQUIRE_EQUAL(docs[i], e.docid(),
                             "i = " << i << " size = " << n);
            MY_REQUIRE_EQUAL(freqs[i], e.freq(),
                             "i = " << i << " size = " << n);
        }
        // XXX better testing of next_geq
        for (size_t i = 0; i < n; ++i) {
            e.reset();
            e.next_geq(docs[i]);
            MY_REQUIRE_EQUAL(docs[i], e.docid(),
                             "i = " << i << " size = " << n);
            MY_REQUIRE_EQUAL(freqs[i], e.freq(),
                             "i = " << i << " size = " << n);
        }
        e.reset(); e.next_geq(docs.back() + 1);
        BOOST_REQUIRE_EQUAL(universe, e.docid());
        e.reset(); e.next_geq(universe);
        BOOST_REQUIRE_EQUAL(universe, e.docid());
    }
}

BOOST_AUTO_TEST_CASE(block_posting_list)
{
    test_block_posting_list<quasi_succinct::optpfor_block>();
    test_block_posting_list<quasi_succinct::varint_G8IU_block>();
    test_block_posting_list<quasi_succinct::interpolative_block>();
}
