#include "block_codecs.hpp"

namespace quasi_succinct {
    optpfor_block::codec_type optpfor_block::optpfor_codec;
    TightVariableByte optpfor_block::vbyte_codec;

    VarIntG8IU varint_G8IU_block::varint_codec;
    TightVariableByte varint_G8IU_block::vbyte_codec;
}
