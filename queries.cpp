#include <iostream>

#include <succinct/mapper.hpp>

#include "index_types.hpp"
#include "wand_data.hpp"
#include "queries.hpp"
#include "util.hpp"

template <typename QueryOperator, typename IndexType>
void op_perftest(IndexType const& index,
                 QueryOperator&& query_op, // XXX!!!
                 std::vector<quasi_succinct::term_id_vec> const& queries,
                 std::string const& index_type,
                 std::string const& query_type,
                 size_t runs)
{
    using namespace quasi_succinct;

    std::vector<double> query_times;

    for (size_t run = 0; run <= runs; ++run) {
        for (auto const& query: queries) {
            auto tick = get_time_usecs();
            uint64_t result = query_op(index, query);
            do_not_optimize_away(result);
            double elapsed = double(get_time_usecs() - tick);
            if (run != 0) { // first run is not timed
                query_times.push_back(elapsed);
            }
        }
    }

    if (false) {
        for (auto t: query_times) {
            std::cout << (t / 1000) << std::endl;
        }
    } else {
        std::sort(query_times.begin(), query_times.end());
        double avg = std::accumulate(query_times.begin(), query_times.end(), double()) / query_times.size();
        double q50 = query_times[query_times.size() / 2];
        double q90 = query_times[90 * query_times.size() / 100];
        double q95 = query_times[95 * query_times.size() / 100];
        logger() << "---- " << index_type << " " << query_type << std::endl;
        logger() << "Mean: " << avg << std::endl;
        logger() << "50% quantile: " << q50 << std::endl;
        logger() << "90% quantile: " << q90 << std::endl;
        logger() << "95% quantile: " << q95 << std::endl;

        stats_line()
            ("type", index_type)
            ("query", query_type)
            ("avg", avg)
            ("q50", q50)
            ("q90", q90)
            ("q95", q95)
            ;
    }
}


template <typename IndexType>
void perftest(const char* index_filename,
              const char* wand_data_filename,
              std::vector<quasi_succinct::term_id_vec> const& queries,
              std::string const& type)
{
    using namespace quasi_succinct;

    IndexType index;
    logger() << "Loading index from " << index_filename << std::endl;
    boost::iostreams::mapped_file_source m(index_filename);
    succinct::mapper::map(index, m, succinct::mapper::map_flags::warmup);

    logger() << "Performing " << type << " queries" << std::endl;
    op_perftest(index, and_query<false>(), queries, type, "and", 3);
    op_perftest(index, and_query<true>(), queries, type, "and_freq", 3);
    op_perftest(index, or_query<false>(), queries, type, "or", 1);
    op_perftest(index, or_query<true>(), queries, type, "or_freq", 1);

    if (wand_data_filename) {
        wand_data<> wdata;
        boost::iostreams::mapped_file_source md(wand_data_filename);
        succinct::mapper::map(wdata, md, succinct::mapper::map_flags::warmup);
        op_perftest(index, ranked_and_query(wdata, 10), queries, type, "ranked_and", 3);
        op_perftest(index, ranked_or_query(wdata, 10), queries, type, "ranked_or", 1);
        op_perftest(index, wand_query(wdata, 10), queries, type, "wand", 1);
        op_perftest(index, maxscore_query(wdata, 10), queries, type, "maxscore", 1);
    }

}

int main(int argc, const char** argv)
{
    using namespace quasi_succinct;

    std::string type = argv[1];
    const char* index_filename = argv[2];
    const char* wand_data_filename = nullptr;
    if (argc >= 4) {
        wand_data_filename = argv[3];
    }

    std::vector<term_id_vec> queries;
    term_id_vec q;
    while (read_query(q)) queries.push_back(q);

    if (false) {
#define LOOP_BODY(R, DATA, T)                                   \
        } else if (type == BOOST_PP_STRINGIZE(T)) {             \
            perftest<BOOST_PP_CAT(T, _index)>                   \
                (index_filename, wand_data_filename, queries, type); \
            /**/

        BOOST_PP_SEQ_FOR_EACH(LOOP_BODY, _, QS_INDEX_TYPES);
#undef LOOP_BODY
    } else {
        logger() << "ERROR: Unknown type " << type << std::endl;
    }

}
