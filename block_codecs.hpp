#pragma once

#include "FastPFor/headers/optpfor.h"
#include "FastPFor/headers/variablebyte.h"
#include "FastPFor/headers/VarIntG8IU.h"

// from integer_encoding_library
#undef ASSERT // XXX WHERE IS THIS DEFINED??
#include "io/BitsReader.hpp"
#include "io/BitsWriter.hpp"

namespace quasi_succinct {

    // workaround: VariableByte::decodeArray needs the buffer size, while we
    // only know the number of values. It also pads to 32 bits. We need to
    // rewrite
    class TightVariableByte {
    public:
        template<uint32_t i>
        static uint8_t extract7bits(const uint32_t val) {
            return static_cast<uint8_t>((val >> (7 * i)) & ((1U << 7) - 1));
        }

        template<uint32_t i>
        static uint8_t extract7bitsmaskless(const uint32_t val) {
            return static_cast<uint8_t>((val >> (7 * i)));
        }

        static void encode(const uint32_t *in, const size_t length,
                           uint8_t *out, size_t& nvalue)
        {
            uint8_t * bout = out;
            for (size_t k = 0; k < length; ++k) {
                const uint32_t val(in[k]);
                /**
                 * Code below could be shorter. Whether it could be faster
                 * depends on your compiler and machine.
                 */
                if (val < (1U << 7)) {
                    *bout = static_cast<uint8_t>(val | (1U << 7));
                    ++bout;
                } else if (val < (1U << 14)) {
                    *bout = extract7bits<0> (val);
                    ++bout;
                    *bout = extract7bitsmaskless<1> (val) | (1U << 7);
                    ++bout;
                } else if (val < (1U << 21)) {
                    *bout = extract7bits<0> (val);
                    ++bout;
                    *bout = extract7bits<1> (val);
                    ++bout;
                    *bout = extract7bitsmaskless<2> (val) | (1U << 7);
                    ++bout;
                } else if (val < (1U << 28)) {
                    *bout = extract7bits<0> (val);
                    ++bout;
                    *bout = extract7bits<1> (val);
                    ++bout;
                    *bout = extract7bits<2> (val);
                    ++bout;
                    *bout = extract7bitsmaskless<3> (val) | (1U << 7);
                    ++bout;
                } else {
                    *bout = extract7bits<0> (val);
                    ++bout;
                    *bout = extract7bits<1> (val);
                    ++bout;
                    *bout = extract7bits<2> (val);
                    ++bout;
                    *bout = extract7bits<3> (val);
                    ++bout;
                    *bout = extract7bitsmaskless<4> (val) | (1U << 7);
                    ++bout;
                }
            }
            nvalue = bout - out;
        }

        static void encode_single(uint32_t val, std::vector<uint8_t>& out)
        {
            uint8_t buf[5];
            size_t nvalue;
            encode(&val, 1, buf, nvalue);
            out.insert(out.end(), buf, buf + nvalue);
        }

        static uint8_t const* decode(const uint8_t *in, uint32_t *out, size_t n)
        {
            const uint8_t * inbyte = in;
            for (size_t i = 0; i < n; ++i) {
                unsigned int shift = 0;
                for (uint32_t v = 0; ; shift += 7) {
                    uint8_t c = *inbyte++;
                    v += ((c & 127) << shift);
                    if ((c & 128)) {
                        *out++ = v;
                        break;
                    }
                }
            }
            return inbyte;
        }
    };

    struct optpfor_block {

        struct codec_type : OPTPFor<4, Simple16<false>> {
            // workaround: OPTPFor does not define decodeBlock, so we cut&paste
            // the code
            uint32_t const* decodeBlock(const uint32_t *in, uint32_t *out, size_t& nvalue)
            {
                const uint32_t * const initout(out);
                const uint32_t b = *in >> (32 - PFORDELTA_B);
                const size_t nExceptions = (*in >> (32 - (PFORDELTA_B
                                                          + PFORDELTA_NEXCEPT))) & ((1 << PFORDELTA_NEXCEPT) - 1);
                const uint32_t encodedExceptionsSize = *in & ((1 << PFORDELTA_EXCEPTSZ)
                                                              - 1);

                size_t twonexceptions = 2 * nExceptions;
                ++in;
                if (encodedExceptionsSize > 0)
                    ecoder.decodeArray(in, encodedExceptionsSize, &exceptions[0],
                                       twonexceptions);
                assert(twonexceptions >= 2 * nExceptions);
                in += encodedExceptionsSize;

                uint32_t * beginout(out);// we use this later

                for (uint32_t j = 0; j < BlockSize; j += 32) {
                    fastunpack(in, out, b);
                    in += b;
                    out += 32;
                }

                for (uint32_t e = 0, lpos = -1; e < nExceptions; e++) {
                    lpos += exceptions[e] + 1;
                    beginout[lpos] |= (exceptions[e + nExceptions] + 1) << b;
                }

                nvalue = out - initout;
                return in;
            }
        };

        static codec_type optpfor_codec;
        static TightVariableByte vbyte_codec;

        static const uint64_t block_size = codec_type::BlockSize;

        static void encode(uint32_t const* in, uint32_t /* sum_of_values */,
                           size_t n, std::vector<uint8_t>& out)
        {
            assert(n <= block_size);
            // XXX this could be threadlocal static
            std::vector<uint8_t> buf(2 * 4 * block_size);
            size_t out_len = buf.size();

            if (n == block_size) {
                optpfor_codec.encodeBlock(in, reinterpret_cast<uint32_t*>(buf.data()),
                                          out_len);
                out_len *= 4;
            } else {
                vbyte_codec.encode(in, n, buf.data(), out_len);
            }
            out.insert(out.end(), buf.data(), buf.data() + out_len);
        }

        static uint8_t const* decode(uint8_t const* in, uint32_t* out,
                                     uint32_t /* sum_of_values */, size_t n)
        {
            assert(n <= block_size);
            size_t out_len = block_size;
            uint8_t const* ret;

            if (n == block_size) {
                ret = reinterpret_cast<uint8_t const*>
                    (optpfor_codec.decodeBlock(reinterpret_cast<uint32_t const*>(in),
                                               out, out_len));
                assert(out_len == n);
            } else {
                ret = vbyte_codec.decode(in, out, n);
            }
            return ret;
        }
    };

    struct varint_G8IU_block {
        static VarIntG8IU varint_codec;
        static TightVariableByte vbyte_codec;

        static const uint64_t block_size = 128;

        static void encode(uint32_t const* in, uint32_t /* sum_of_values */,
                           size_t n, std::vector<uint8_t>& out)
        {
            assert(n <= block_size);
            // XXX this could be threadlocal static
            std::vector<uint8_t> buf(2 * 4 * block_size);
            size_t out_len = buf.size();

            if (n == block_size) {
                const uint32_t * src = in;
                unsigned char* dst = buf.data();
                size_t srclen = n * 4;
                size_t dstlen = out_len;
                out_len = 0;
                while (srclen > 0 && dstlen >= 9) {
                    out_len += varint_codec.encodeBlock(src, srclen, dst, dstlen);
                }
                assert(srclen == 0);
            } else {
                vbyte_codec.encode(in, n, buf.data(), out_len);
            }
            out.insert(out.end(), buf.data(), buf.data() + out_len);
        }

        static uint8_t const* decode(uint8_t const* in, uint32_t* out,
                                     uint32_t /* sum_of_values */, size_t n)
        {
            assert(n <= block_size);
            size_t out_len = block_size;
            uint8_t const* ret;

            if (n == block_size) {
                const uint8_t * src = in;
                uint32_t* dst = out;
                size_t srclen = 2 * out_len * 4; // upper bound
                size_t dstlen = out_len * 4;
                out_len = 0;
                while (out_len <= (n - 8)) {
                    out_len += varint_codec.decodeBlock(src, srclen, dst, dstlen);
                }

                // decodeBlock can overshoot, so we decode the last blocks in a
                // local buffer
                while (out_len < n) {
                    uint32_t buf[8];
                    uint32_t* bufptr = buf;
                    size_t buflen = 8 * 4;
                    size_t read = varint_codec.decodeBlock(src, srclen, bufptr, buflen);
                    size_t needed = std::min(read, n - out_len);
                    memcpy(dst, buf, needed * 4);
                    dst += needed;
                    out_len += needed;
                }
                assert(out_len == n);
                ret = src;
            } else {
                ret = vbyte_codec.decode(in, out, n);
            }
            return ret;
        }
    };

    struct interpolative_block {
        static const uint64_t block_size = 128;

        static void encode(uint32_t const* in, uint32_t sum_of_values,
                           size_t n, std::vector<uint8_t>& out)
        {
            assert(n <= block_size);
            std::vector<uint32_t> inbuf(n);
            inbuf[0] = *in;
            for (size_t i = 1; i < n; ++i) {
                inbuf[i] = inbuf[i - 1] + in[i] + 1;
            }
            std::vector<uint32_t> buf(2 * block_size);
            if (sum_of_values == uint32_t(-1)) {
                sum_of_values = inbuf.back() - (n - 1);
                TightVariableByte::encode_single(sum_of_values, out);
            }

            if (n > 1) {
                uint32_t high = sum_of_values + n - 1;
                integer_encoding::internals::BitsWriter bw(buf.data(), buf.size());
                bw.intrpolatvArray(inbuf.data(), n - 1, 0, 0, high);
                bw.flush_bits();
                uint8_t const* bufptr = (uint8_t const*)buf.data();
                out.insert(out.end(), bufptr, bufptr + bw.size() * 4); // XXX wasting one word!
            }
        }

        static uint8_t const* decode(uint8_t const* in, uint32_t* out,
                                     uint32_t sum_of_values, size_t n)
        {
            assert(n <= block_size);
            uint8_t const* inbuf = in;
            if (sum_of_values == uint32_t(-1)) {
                inbuf = TightVariableByte::decode(inbuf, &sum_of_values, 1);
            }

            uint32_t high = sum_of_values + n - 1;
            out[n - 1] = high;
            if (n > 1) {
                integer_encoding::internals::BitsReader br((uint32_t const*)inbuf, 2 * n);
                br.intrpolatvArray(out, n - 1, 0, 0, high);
                for (size_t i = n - 1; i > 0; --i) {
                    out[i] -= out[i - 1] + 1;
                }
                return (uint8_t const*)(br.pos() + 1);
            } else {
                return inbuf;
            }
        }
    };
}
