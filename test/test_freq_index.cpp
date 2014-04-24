#define BOOST_TEST_MODULE freq_index

#include "test_generic_sequence.hpp"

#include "freq_index.hpp"
#include "indexed_sequence.hpp"
#include "partitioned_sequence.hpp"
#include "positive_sequence.hpp"
#include "uniform_partitioned_sequence.hpp"
#include <succinct/mapper.hpp>

#include <vector>
#include <cstdlib>
#include <algorithm>
#include <numeric>

template <typename DocsSequence, typename FreqsSequence>
void test_freq_index()
{
    quasi_succinct::global_parameters params;
    uint64_t universe = 20000;
    typedef quasi_succinct::freq_index<DocsSequence, FreqsSequence>
        collection_type;
    typename collection_type::builder b(universe, params);

    typedef std::vector<uint64_t> vec_type;
    std::vector<std::pair<vec_type, vec_type>> posting_lists(30);
    for (auto& plist: posting_lists) {
        double avg_gap = 1.1 + double(rand()) / RAND_MAX * 10;
        uint64_t n = uint64_t(universe / avg_gap);
        plist.first = random_sequence(universe, n, true);
        plist.second.resize(n);
        std::generate(plist.second.begin(), plist.second.end(),
                      []() { return (rand() % 256) + 1; });
        uint64_t freqs_sum = std::accumulate(plist.second.begin(),
                                             plist.second.end(), uint64_t(0));

        b.add_posting_list(n, plist.first.begin(),
                           plist.second.begin(), freqs_sum);

    }

    {
        collection_type coll;
        b.build(coll);
        succinct::mapper::freeze(coll, "temp.bin");
    }

    {
        collection_type coll;
        boost::iostreams::mapped_file_source m("temp.bin");
        succinct::mapper::map(coll, m);

        for (size_t i = 0; i < posting_lists.size(); ++i) {
            auto const& plist = posting_lists[i];
            auto doc_enum = coll[i];
            BOOST_REQUIRE_EQUAL(plist.first.size(), doc_enum.size());
            for (size_t p = 0; p < plist.first.size(); ++p, doc_enum.next()) {
                MY_REQUIRE_EQUAL(plist.first[p], doc_enum.docid(),
                                 "i = " << i << " p = " << p);
                MY_REQUIRE_EQUAL(plist.second[p], doc_enum.freq(),
                                 "i = " << i << " p = " << p);
            }
            BOOST_REQUIRE_EQUAL(coll.num_docs(), doc_enum.docid());
        }
    }
}

BOOST_AUTO_TEST_CASE(freq_index)
{
    using quasi_succinct::indexed_sequence;
    using quasi_succinct::strict_sequence;
    using quasi_succinct::positive_sequence;
    using quasi_succinct::partitioned_sequence;
    using quasi_succinct::uniform_partitioned_sequence;

    test_freq_index<indexed_sequence,
                    positive_sequence<>>();

    test_freq_index<partitioned_sequence<>,
                    positive_sequence<partitioned_sequence<strict_sequence>>>();
    test_freq_index<uniform_partitioned_sequence<>,
                    positive_sequence<uniform_partitioned_sequence<strict_sequence>>>();
}
