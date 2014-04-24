#pragma once

#include "succinct/util.hpp"
#include "block_codecs.hpp"
#include "util.hpp"

namespace quasi_succinct {

    template <typename BlockCodec>
    struct block_posting_list {

        template <typename DocsIterator, typename FreqsIterator>
        static void write(std::vector<uint8_t>& out, uint32_t n,
                          DocsIterator docs_begin, FreqsIterator freqs_begin) {
            TightVariableByte::encode_single(n, out);

            uint64_t block_size = BlockCodec::block_size;
            uint64_t blocks = succinct::util::ceil_div(n, block_size);
            size_t begin_block_maxs = out.size();
            size_t begin_block_endpoints = begin_block_maxs + 4 * blocks;
            size_t begin_blocks = begin_block_endpoints + 4 * (blocks - 1);
            out.resize(begin_blocks);

            DocsIterator docs_it(docs_begin);
            FreqsIterator freqs_it(freqs_begin);
            std::vector<uint32_t> docs_buf(block_size);
            std::vector<uint32_t> freqs_buf(block_size);
            uint32_t last_doc(-1);
            uint32_t block_base = 0;
            for (size_t b = 0; b < blocks; ++b) {
                uint32_t cur_block_size =
                    ((b + 1) * block_size <= n)
                    ? block_size : (n % block_size);

                for (size_t i = 0; i < cur_block_size; ++i) {
                    uint32_t doc(*docs_it++);
                    docs_buf[i] = doc - last_doc - 1;
                    last_doc = doc;

                    freqs_buf[i] = *freqs_it++ - 1;
                }
                *((uint32_t*)&out[begin_block_maxs + 4 * b]) = last_doc;

                BlockCodec::encode(docs_buf.data(), last_doc - block_base - (cur_block_size - 1),
                                   cur_block_size, out);
                BlockCodec::encode(freqs_buf.data(), uint32_t(-1), cur_block_size, out);
                if (b != blocks - 1) {
                    *((uint32_t*)&out[begin_block_endpoints + 4 * b]) = out.size() - begin_blocks;
                }
                block_base = last_doc + 1;
            }
        }

        class document_enumerator {
        public:
            document_enumerator(uint8_t const* data, uint64_t universe)
                : m_n(0) // just to silence warnings
                , m_base(TightVariableByte::decode(data, &m_n, 1))
                , m_blocks(succinct::util::ceil_div(m_n, BlockCodec::block_size))
                , m_block_maxs(m_base)
                , m_block_endpoints(m_block_maxs + 4 * m_blocks)
                , m_blocks_data(m_block_endpoints + 4 * (m_blocks - 1))
                , m_universe(universe)
            {
                m_docs_buf.resize(BlockCodec::block_size);
                m_freqs_buf.resize(BlockCodec::block_size);
                reset();
            }

            void reset()
            {
                decode_docs_block(0);
            }

            void QS_ALWAYSINLINE next()
            {
                ++m_pos_in_block;
                if (QS_UNLIKELY(m_pos_in_block == m_cur_block_size)) {
                    if (m_cur_block + 1 == m_blocks) {
                        m_cur_docid = m_universe;
                        return;
                    }
                    decode_docs_block(m_cur_block + 1);
                } else {
                    m_cur_docid += m_docs_buf[m_pos_in_block] + 1;
                }
            }

            void QS_ALWAYSINLINE next_geq(uint64_t lower_bound)
            {
                assert(lower_bound >= m_cur_docid);
                if (QS_UNLIKELY(lower_bound > m_cur_block_max)) {
                    // binary search seems to perform worse here
                    if (lower_bound > block_max(m_blocks - 1)) {
                        m_cur_docid = m_universe;
                        return;
                    }

                    uint64_t block = m_cur_block + 1;
                    while (block_max(block) < lower_bound) {
                        ++block;
                    }

                    decode_docs_block(block);
                }

                while (docid() < lower_bound) {
                    m_cur_docid += m_docs_buf[++m_pos_in_block] + 1;
                    assert(m_pos_in_block < m_cur_block_size);
                }
            }

            void QS_ALWAYSINLINE move(uint64_t pos)
            {
                assert(pos >= position());
                uint64_t block = pos / BlockCodec::block_size;
                if (QS_UNLIKELY(block != m_cur_block)) {
                    decode_docs_block(block);
                }
                while (position() < pos) {
                    m_cur_docid += m_docs_buf[++m_pos_in_block] + 1;
                }
            }

            uint64_t docid() const
            {
                return m_cur_docid;
            }

            uint64_t QS_ALWAYSINLINE freq()
            {
                if (!m_freqs_decoded) {
                    decode_freqs_block();
                }
                return m_freqs_buf[m_pos_in_block] + 1;
            }

            uint64_t position() const
            {
                return m_cur_block * BlockCodec::block_size + m_pos_in_block;
            }

            uint64_t size() const
            {
                return m_n;
            }

            uint64_t stats_freqs_size() const
            {
                uint64_t bytes = 0;
                uint8_t const* ptr = m_blocks_data;
                static const uint64_t block_size = BlockCodec::block_size;
                std::vector<uint32_t> buf(block_size);
                for (size_t b = 0; b < m_blocks; ++b) {
                    uint32_t cur_block_size =
                        ((b + 1) * block_size <= size())
                        ? block_size : (size() % block_size);

                    uint32_t cur_base = (b ? block_max(b - 1) : uint32_t(-1)) + 1;
                    uint8_t const* freq_ptr =
                        BlockCodec::decode(ptr, buf.data(),
                                           block_max(b) - cur_base - (cur_block_size - 1),
                                           cur_block_size);
                    ptr = BlockCodec::decode(freq_ptr, buf.data(),
                                             uint32_t(-1), cur_block_size);
                    bytes += ptr - freq_ptr;
                }

                return bytes;
            }

        private:
            uint32_t block_max(uint32_t block) const
            {
                return ((uint32_t const*)m_block_maxs)[block];
            }

            void QS_NOINLINE decode_docs_block(uint64_t block)
            {
                static const uint64_t block_size = BlockCodec::block_size;
                uint32_t endpoint = block
                    ? ((uint32_t const*)m_block_endpoints)[block - 1]
                    : 0;
                uint8_t const* block_data = m_blocks_data + endpoint;
                m_cur_block_size =
                    ((block + 1) * block_size <= size())
                    ? block_size : (size() % block_size);
                uint32_t cur_base = (block ? block_max(block - 1) : uint32_t(-1)) + 1;
                m_cur_block_max = block_max(block);
                m_freqs_block_data =
                    BlockCodec::decode(block_data, m_docs_buf.data(),
                                       m_cur_block_max - cur_base - (m_cur_block_size - 1),
                                       m_cur_block_size);

                m_docs_buf[0] += cur_base;

                m_cur_block = block;
                m_pos_in_block = 0;
                m_cur_docid = m_docs_buf[0];
                m_freqs_decoded = false;
            }

            void QS_NOINLINE decode_freqs_block()
            {
                BlockCodec::decode(m_freqs_block_data, m_freqs_buf.data(),
                                   uint32_t(-1), m_cur_block_size);
                m_freqs_decoded = true;
            }

            uint32_t m_n;
            uint8_t const* m_base;
            uint32_t m_blocks;
            uint8_t const* m_block_maxs;
            uint8_t const* m_block_endpoints;
            uint8_t const* m_blocks_data;
            uint64_t m_universe;

            uint32_t m_cur_block;
            uint32_t m_pos_in_block;
            uint32_t m_cur_block_max;
            uint32_t m_cur_block_size;
            uint32_t m_cur_docid;

            uint8_t const* m_freqs_block_data;
            bool m_freqs_decoded;

            std::vector<uint32_t> m_docs_buf;
            std::vector<uint32_t> m_freqs_buf;
        };

    };
}
