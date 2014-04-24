#pragma once

#include <cstdlib>
#include <cstdint>
#include <thread>
#include <boost/lexical_cast.hpp>

namespace quasi_succinct {

    class configuration {
    public:
        static configuration const& get() {
            static configuration instance;
            return instance;
        }

        double eps1;
        double eps2;
        uint64_t fix_cost;

        size_t log_partition_size;
        size_t worker_threads;

    private:
        configuration()
        {
            fillvar("QS_EPS1", eps1, 0.03);
            fillvar("QS_EPS2", eps2, 0.3);
            fillvar("QS_FIXCOST", fix_cost, 64);
            fillvar("QS_LOG_PART", log_partition_size, 7);
            fillvar("QS_THREADS", worker_threads, std::thread::hardware_concurrency());
        }

        template <typename T, typename T2>
        void fillvar(const char* envvar, T& var, T2 def)
        {
            const char* val = std::getenv(envvar);
            if (!val || !strlen(val)) {
                var = def;
            } else {
                var = boost::lexical_cast<T>(val);
            }
        }
    };

}
