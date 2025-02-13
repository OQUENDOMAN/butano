/*
 * Copyright (c) 2020-2021 Gustavo Valiente gustavo.valiente@protonmail.com
 * zlib License, see LICENSE file.
 */

#ifndef BN_HW_UNCOMPRESS_H
#define BN_HW_UNCOMPRESS_H

#include "bn_common.h"
#include "../3rd_party/cult-of-gba-bios/include/cult-of-gba-bios.h"

namespace bn::hw::uncompress
{
    inline void lz77_wram(const void* src, void* dst)
    {
        swi_LZ77UnCompWrite8bit(src, dst);
    }

    inline void lz77_vram(const void* src, void* dst)
    {
        swi_LZ77UnCompWrite16bit(src, dst);
    }

    inline void rl_wram(const void* src, void* dst)
    {
        swi_RLUnCompReadNormalWrite8bit(src, dst);
    }

    inline void rl_vram(const void* src, void* dst)
    {
        swi_RLUnCompReadNormalWrite16bit(src, dst);
    }
}

#endif
