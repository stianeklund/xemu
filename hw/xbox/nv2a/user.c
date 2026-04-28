/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2021 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "nv2a_int.h"

static PFIFOCache1Context *pfifo_user_context_for_chid(NV2AState *d,
                                                        unsigned int chid)
{
    assert(chid < NV2A_NUM_CHANNELS);
    return &d->pfifo.cache1_context[chid];
}

static void pfifo_user_context_ensure_valid(PFIFOCache1Context *ctx)
{
    if (ctx->valid) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->valid = true;
}

static uint32_t pfifo_user_dma_bit_for_chid(NV2AState *d, unsigned int chid,
                                            uint32_t dma_put, uint32_t dma_get)
{
    uint32_t dma = d->pfifo.regs[NV_PFIFO_DMA] & ~(1u << chid);
    if (dma_put != dma_get) {
        dma |= (1u << chid);
    }
    return dma;
}

/* USER - PFIFO MMIO and DMA submission area */
uint64_t user_read(void *opaque, hwaddr addr, unsigned int size)
{
    NV2AState *d = (NV2AState *)opaque;

    unsigned int channel_id = addr >> 16;
    assert(channel_id < NV2A_NUM_CHANNELS);

    qemu_mutex_lock(&d->pfifo.lock);

    uint32_t channel_modes = d->pfifo.regs[NV_PFIFO_MODE];

    uint64_t r = 0;
    if (channel_modes & (1 << channel_id)) {
        /* DMA Mode */

        unsigned int cur_channel_id =
            GET_MASK(d->pfifo.regs[NV_PFIFO_CACHE1_PUSH1],
                     NV_PFIFO_CACHE1_PUSH1_CHID);

        if (channel_id == cur_channel_id) {
            switch (addr & 0xFFFF) {
            case NV_USER_DMA_PUT:
                r = d->pfifo.regs[NV_PFIFO_CACHE1_DMA_PUT];
                break;
            case NV_USER_DMA_GET:
                r = d->pfifo.regs[NV_PFIFO_CACHE1_DMA_GET];
                break;
            case NV_USER_REF:
                r = d->pfifo.regs[NV_PFIFO_CACHE1_REF];
                break;
            default:
                break;
            }
        } else {
            /* Reads to non-current channels access stored channel context. */
            PFIFOCache1Context *ctx =
                pfifo_user_context_for_chid(d, channel_id);
            if (ctx->valid) {
                switch (addr & 0xFFFF) {
                case NV_USER_DMA_PUT:
                    r = ctx->dma_put;
                    break;
                case NV_USER_DMA_GET:
                    r = ctx->dma_get;
                    break;
                case NV_USER_REF:
                    r = ctx->ref;
                    break;
                default:
                    break;
                }
            }
        }
    } else {
        /* PIO Mode */
        assert(false);
    }

    qemu_mutex_unlock(&d->pfifo.lock);

    nv2a_reg_log_read(NV_USER, addr, size, r);
    return r;
}

void user_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    NV2AState *d = (NV2AState *)opaque;

    nv2a_reg_log_write(NV_USER, addr, size, val);

    unsigned int channel_id = addr >> 16;
    assert(channel_id < NV2A_NUM_CHANNELS);

    qemu_mutex_lock(&d->pfifo.lock);

    uint32_t channel_modes = d->pfifo.regs[NV_PFIFO_MODE];
    if (channel_modes & (1 << channel_id)) {
        /* DMA Mode */
        unsigned int cur_channel_id =
            GET_MASK(d->pfifo.regs[NV_PFIFO_CACHE1_PUSH1],
                     NV_PFIFO_CACHE1_PUSH1_CHID);

        if (channel_id == cur_channel_id) {
            PFIFOCache1Context *ctx =
                pfifo_user_context_for_chid(d, channel_id);
            pfifo_user_context_ensure_valid(ctx);

            switch (addr & 0xFFFF) {
            case NV_USER_DMA_PUT:
                d->pfifo.regs[NV_PFIFO_CACHE1_DMA_PUT] = val;
                ctx->dma_put = val;
                break;
            case NV_USER_DMA_GET:
                d->pfifo.regs[NV_PFIFO_CACHE1_DMA_GET] = val;
                ctx->dma_get = val;
                break;
            case NV_USER_REF:
                d->pfifo.regs[NV_PFIFO_CACHE1_REF] = val;
                ctx->ref = val;
                break;
            default:
                assert(false);
                break;
            }

            d->pfifo.regs[NV_PFIFO_DMA] = pfifo_user_dma_bit_for_chid(
                d, channel_id, d->pfifo.regs[NV_PFIFO_CACHE1_DMA_PUT],
                d->pfifo.regs[NV_PFIFO_CACHE1_DMA_GET]);
            pfifo_kick(d);

        } else {
            /*
             * Writes to non-current channels update stored channel context.
             * Real hardware routes these through RAMFC.
             */
            PFIFOCache1Context *ctx =
                pfifo_user_context_for_chid(d, channel_id);
            pfifo_user_context_ensure_valid(ctx);

            switch (addr & 0xFFFF) {
            case NV_USER_DMA_PUT:
                ctx->dma_put = val;
                break;
            case NV_USER_DMA_GET:
                ctx->dma_get = val;
                break;
            case NV_USER_REF:
                ctx->ref = val;
                break;
            default:
                assert(false);
                break;
            }

            d->pfifo.regs[NV_PFIFO_DMA] = pfifo_user_dma_bit_for_chid(
                d, channel_id, ctx->dma_put, ctx->dma_get);
        }
    } else {
        /* PIO Mode */
        assert(false);
    }

    qemu_mutex_unlock(&d->pfifo.lock);

}
