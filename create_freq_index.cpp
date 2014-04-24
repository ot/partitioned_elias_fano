#include <fstream>
#include <iostream>
#include <algorithm>
#include <thread>
#include <numeric>

#include <succinct/mapper.hpp>

#include "configuration.hpp"
#include "index_types.hpp"
#include "util.hpp"

using quasi_succinct::logger;

template <typename InputCollection, typename Collection>
void verify_collection(InputCollection const& input, const char* filename)
{
    Collection coll;
    boost::iostreams::mapped_file_source m(filename);
    succinct::mapper::map(coll, m);

    logger() << "Checking the written data, just to be extra safe..." << std::endl;
    size_t s = 0;
    for (auto seq: input) {
        auto e = coll[s];
        if (e.size() != seq.docs.size()) {
            logger() << "sequence " << s
                     << " has wrong length! ("
                     << e.size() << " != " << seq.docs.size() << ")"
                     << std::endl;
            exit(1);
        }

        for (size_t i = 0; i < e.size(); ++i, e.next()) {
            uint64_t docid = *(seq.docs.begin() + i);
            uint64_t freq = *(seq.freqs.begin() + i);

            if (docid != e.docid()) {
                logger() << "docid in sequence " << s
                         << " differs at position " << i << "!" << std::endl;
                logger() << e.docid() << " != " << docid << std::endl;
                logger() << "sequence length: " << seq.docs.size() << std::endl;

                exit(1);
            }

            if (freq != e.freq()) {
                logger() << "freq in sequence " << s
                         << " differs at position " << i << "!" << std::endl;
                logger() << e.freq() << " != " << freq << std::endl;
                logger() << "sequence length: " << seq.docs.size() << std::endl;

                exit(1);
            }
        }

        s += 1;
    }
    logger() << "Everything is OK!" << std::endl;
}


template <typename DocsSequence, typename FreqsSequence>
void get_size_stats(quasi_succinct::freq_index<DocsSequence, FreqsSequence>& coll,
                    uint64_t& docs_size, uint64_t& freqs_size)
{
    auto size_tree = succinct::mapper::size_tree_of(coll);
    size_tree->dump();
    for (auto const& node: size_tree->children) {
        if (node->name == "m_docs_sequences") {
            docs_size = node->size;
        } else if (node->name == "m_freqs_sequences") {
            freqs_size = node->size;
        }
    }
}

template <typename BlockCodec>
void get_size_stats(quasi_succinct::block_freq_index<BlockCodec>& coll,
                    uint64_t& docs_size, uint64_t& freqs_size)
{
    auto size_tree = succinct::mapper::size_tree_of(coll);
    size_tree->dump();
    uint64_t total_size = size_tree->size;
    freqs_size = 0;
    for (size_t i = 0; i < coll.size(); ++i) {
        freqs_size += coll[i].stats_freqs_size();
    }
    docs_size = total_size - freqs_size;
}

template <typename Collection>
void dump_stats(Collection& coll,
                std::string const& type,
                uint64_t postings)
{

    uint64_t docs_size = 0, freqs_size = 0;
    get_size_stats(coll, docs_size, freqs_size);

    double bits_per_doc = docs_size * 8.0 / postings;
    double bits_per_freq = freqs_size * 8.0 / postings;
    logger() << "Documents: " << docs_size << " bytes, "
             << bits_per_doc << " bits per element" << std::endl;
    logger() << "Frequencies: " << freqs_size << " bytes, "
             << bits_per_freq << " bits per element" << std::endl;

    quasi_succinct::stats_line()
        ("type", type)
        ("docs_size", docs_size)
        ("freqs_size", freqs_size)
        ("bits_per_doc", bits_per_doc)
        ("bits_per_freq", bits_per_freq)
        ;
}

template <typename Collection>
void dump_index_specific_stats(Collection const&, std::string const&)
{}


void dump_index_specific_stats(quasi_succinct::uniform_index const& coll,
                               std::string const& type)
{
    quasi_succinct::stats_line()
        ("type", type)
        ("log_partition_size", int(coll.params().log_partition_size))
        ;
}


void dump_index_specific_stats(quasi_succinct::opt_index const& coll,
                               std::string const& type)
{
    auto const& conf = quasi_succinct::configuration::get();

    uint64_t length_threshold = 4096;
    double long_postings = 0;
    double docs_partitions = 0;
    double freqs_partitions = 0;

    for (size_t s = 0; s < coll.size(); ++s) {
        auto const& list = coll[s];
        if (list.size() >= length_threshold) {
            long_postings += list.size();
            docs_partitions += list.docs_enum().num_partitions();
            freqs_partitions += list.freqs_enum().base().num_partitions();
        }
    }

    quasi_succinct::stats_line()
        ("type", type)
        ("eps1", conf.eps1)
        ("eps2", conf.eps2)
        ("fix_cost", conf.fix_cost)
        ("docs_avg_part", long_postings / docs_partitions)
        ("freqs_avg_part", long_postings / freqs_partitions)
        ;
}


struct progress_logger {
    progress_logger()
        : sequences(0)
        , postings(0)
    {}

    void log()
    {
        logger() << "Processed " << sequences << " sequences, "
                 << postings << " postings" << std::endl;
    }

    void done_sequence(size_t n)
    {
        sequences += 1;
        postings += n;
        if (sequences % 1000000 == 0) {
            log();
        }
    }

    size_t sequences, postings;
};

template <typename InputCollection, typename CollectionType>
void create_collection(InputCollection const& input,
                       quasi_succinct::global_parameters const& params,
                       const char* output_filename, bool check,
                       std::string const& seq_type)
{
    using namespace quasi_succinct;

    logger() << "Processing " << input.num_docs() << " documents" << std::endl;
    double tick = get_time_usecs();
    double user_tick = get_user_time_usecs();

    typename CollectionType::builder builder(input.num_docs(), params);
    progress_logger plog;
    for (auto const& plist: input) {
        uint64_t freqs_sum = std::accumulate(plist.freqs.begin(),
                                             plist.freqs.end(), uint64_t(0));

        builder.add_posting_list(plist.docs.size(), plist.docs.begin(),
                                 plist.freqs.begin(), freqs_sum);
        plog.done_sequence(plist.docs.size());
    }

    plog.log();
    CollectionType coll;
    builder.build(coll);
    double elapsed_secs = (get_time_usecs() - tick) / 1000000;
    double user_elapsed_secs = (get_user_time_usecs() - user_tick) / 1000000;
    logger() << seq_type << " collection built in "
             << elapsed_secs << " seconds" << std::endl;

    stats_line()
        ("type", seq_type)
        ("worker_threads", configuration::get().worker_threads)
        ("construction_time", elapsed_secs)
        ("construction_user_time", user_elapsed_secs)
        ;

    dump_stats(coll, seq_type, plog.postings);
    dump_index_specific_stats(coll, seq_type);

    if (output_filename) {
        succinct::mapper::freeze(coll, output_filename);
        if (check) {
            verify_collection<InputCollection, CollectionType>(input, output_filename);
        }
    }
}


int main(int argc, const char** argv) {

    using namespace quasi_succinct;

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <index type> <collection basename> [<output filename>]"
                  << std::endl;
        return 1;
    }

    std::string type = argv[1];
    const char* input_basename = argv[2];
    const char* output_filename = nullptr;
    if (argc > 3) {
        output_filename = argv[3];
    }

    bool check = false;
    if (argc > 4 && std::string(argv[4]) == "--check") {
        check = true;
    }

    binary_freq_collection input(input_basename);
    quasi_succinct::global_parameters params;
    params.log_partition_size = configuration::get().log_partition_size;

    if (false) {
#define LOOP_BODY(R, DATA, T)                                   \
        } else if (type == BOOST_PP_STRINGIZE(T)) {             \
            create_collection<binary_freq_collection,           \
                              BOOST_PP_CAT(T, _index)>          \
                (input, params, output_filename, check, type);  \
            /**/

        BOOST_PP_SEQ_FOR_EACH(LOOP_BODY, _, QS_INDEX_TYPES);
#undef LOOP_BODY
    } else {
        logger() << "ERROR: Unknown type " << type << std::endl;
    }

    return 0;
}
