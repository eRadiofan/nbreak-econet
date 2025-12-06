/*
 * EconetWiFi - PARLIO DRIVER
 * Copyright (c) 2022-2025 Espressif Systems (Shanghai) CO LTD
 * Copyright (c) 2025 Paul G. Banks <https://paulbanks.org/projects/econet>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the LICENSE file in the project root for full license information.
 */

#include "esp_rom_gpio.h"
#include "esp_intr_alloc.h"
#include "driver/gpio.h"
#include "driver/parlio_tx.h"
#include "parlio_tx_econet_priv.h"

void parlio_tx_neg_edge(parlio_tx_unit_t *tx_unit)
{
    int group_id = tx_unit->base.group->group_id;
    int unit_id = tx_unit->base.unit_id;
    esp_rom_gpio_connect_in_signal(tx_unit->clk_in_gpio_num,
                                   parlio_periph_signals.groups[group_id].tx_units[unit_id].clk_in_sig, true);
}

static void parlio_mount_buffer(parlio_tx_unit_t *tx_unit, parlio_tx_trans_desc_t *t)
{
    // DMA transfer data based on bytes not bits, so convert the bit length to bytes, round up
    gdma_buffer_mount_config_t mount_config = {
        .buffer = (void *)t->payload,
        .length = (t->payload_bits + 7) / 8,
        .flags = {
            // if transmission is loop, we don't need to generate the EOF for 1-bit data width, DIG-559
            .mark_eof = tx_unit->data_width == 1 ? !t->flags.loop_transmission : true,
            .mark_final = !t->flags.loop_transmission,
        }};

    int next_link_idx = t->flags.loop_transmission ? 1 - t->dma_link_idx : t->dma_link_idx;
    gdma_link_mount_buffers(tx_unit->dma_link[next_link_idx], 0, &mount_config, 1, NULL);

    if (t->flags.loop_transmission)
    {
        // concatenate the DMA linked list of the next frame transmission with the DMA linked list of the current frame to realize the reuse of the current transmission transaction
        gdma_link_concat(tx_unit->dma_link[t->dma_link_idx], -1, tx_unit->dma_link[next_link_idx], 0);
        t->dma_link_idx = next_link_idx;
    }
}

static void parlio_tx_queue_transaction(parlio_tx_unit_t *tx_unit, parlio_tx_trans_desc_t *t)
{
    parlio_hal_context_t *hal = &tx_unit->base.group->hal;
    tx_unit->cur_trans = t;

    // If the external clock is a non-free-running clock, it needs to be switched to the internal free-running clock first.
    // And then switched back to the actual clock after the reset is completed.
    bool switch_clk = tx_unit->clk_src == PARLIO_CLK_SRC_EXTERNAL ? true : false;
    if (switch_clk)
    {
        PARLIO_CLOCK_SRC_ATOMIC()
        {
            parlio_ll_tx_set_clock_source(hal->regs, PARLIO_CLK_SRC_XTAL);
        }
    }
    PARLIO_RCC_ATOMIC()
    {
        parlio_ll_tx_reset_clock(hal->regs);
    }
    // Since the threshold of the clock divider counter is not updated simultaneously with the clock source switching.
    // The update of the threshold relies on the moment when the counter reaches the threshold each time.
    // We place parlio_mount_buffer between reset clock and disable clock to ensure enough time for updating the threshold of the clock divider counter.
    parlio_mount_buffer(tx_unit, t);
    if (switch_clk)
    {
        PARLIO_CLOCK_SRC_ATOMIC()
        {
            parlio_ll_tx_set_clock_source(hal->regs, PARLIO_CLK_SRC_EXTERNAL);
        }
    }
    PARLIO_CLOCK_SRC_ATOMIC()
    {
        parlio_ll_tx_enable_clock(hal->regs, false);
    }
    // reset tx fifo after disabling tx core clk to avoid unexpected rempty interrupt
    parlio_ll_tx_reset_fifo(hal->regs);
    parlio_ll_tx_set_idle_data_value(hal->regs, t->idle_value);

    // set EOF condition
    if (t->flags.loop_transmission)
    {
        if (tx_unit->data_width == 1)
        {
            // for 1-bit data width, we need to set the EOF condition to DMA EOF
            parlio_ll_tx_set_eof_condition(hal->regs, PARLIO_LL_TX_EOF_COND_DMA_EOF);
        }
        else
        {
            // for other data widths, we still use the data length EOF condition,
            // but let the `bit counter` + `data width` for each cycle is never equal to the configured bit lens.
            // Thus, we can skip the exact match, prevents EOF
            parlio_ll_tx_set_eof_condition(hal->regs, PARLIO_LL_TX_EOF_COND_DATA_LEN);
            parlio_ll_tx_set_trans_bit_len(hal->regs, 0x01);
        }
    }
    else
    {
        // non-loop transmission
#if SOC_PARLIO_TX_SUPPORT_EOF_FROM_DMA
        // for DMA EOF supported target, we need to set the EOF condition to DMA EOF
        parlio_ll_tx_set_eof_condition(hal->regs, PARLIO_LL_TX_EOF_COND_DMA_EOF);
#else
        // for DMA EOF not supported target, we need to set the bit length to the configured bit lens
        parlio_ll_tx_set_eof_condition(hal->regs, PARLIO_LL_TX_EOF_COND_DATA_LEN);
        parlio_ll_tx_set_trans_bit_len(hal->regs, t->payload_bits);
#endif // SOC_PARLIO_TX_SUPPORT_EOF_FROM_DMA
    }

    if (tx_unit->bs_handle)
    {
        // load the bitscrambler program and start it
        tx_unit->bs_enable_fn(tx_unit, t);
    }

    gdma_start(tx_unit->dma_chan, gdma_link_get_head_addr(tx_unit->dma_link[t->dma_link_idx]));
    // wait until the data goes from the DMA to TX unit's FIFO
    while (parlio_ll_tx_is_ready(hal->regs) == false)
        ;
    // turn on the core clock after we start the TX unit
    //  parlio_ll_tx_start(hal->regs, true);
    //  PARLIO_CLOCK_SRC_ATOMIC() {
    //      parlio_ll_tx_enable_clock(hal->regs, true);
    //  }
}

void IRAM_ATTR parlio_tx_go(parlio_tx_unit_handle_t tx_unit)
{
    parlio_hal_context_t *hal = &tx_unit->base.group->hal;
    // turn on the core clock after we start the TX unit
    parlio_ll_tx_start(hal->regs, true);
    PARLIO_CLOCK_SRC_ATOMIC()
    {
        parlio_ll_tx_enable_clock(hal->regs, true);
    }
}

esp_err_t parlio_tx_unit_pretransmit(parlio_tx_unit_handle_t tx_unit, const void *payload, size_t payload_bits, const parlio_transmit_config_t *config)
{
    ESP_RETURN_ON_FALSE(tx_unit && payload && payload_bits, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE((payload_bits % tx_unit->data_width) == 0, ESP_ERR_INVALID_ARG, TAG, "payload bit length must align to bus width");
    ESP_RETURN_ON_FALSE(payload_bits <= tx_unit->max_transfer_bits, ESP_ERR_INVALID_ARG, TAG, "payload bit length too large");
#if !SOC_PARLIO_TRANS_BIT_ALIGN
    ESP_RETURN_ON_FALSE((payload_bits % 8) == 0, ESP_ERR_INVALID_ARG, TAG, "payload bit length must be multiple of 8");
#endif // !SOC_PARLIO_TRANS_BIT_ALIGN

#if SOC_PARLIO_TX_SUPPORT_LOOP_TRANSMISSION
    if (config->flags.loop_transmission)
    {
        ESP_RETURN_ON_FALSE(parlio_ll_tx_support_dma_eof(NULL) || tx_unit->data_width > 1, ESP_ERR_NOT_SUPPORTED, TAG,
                            "1-bit data width loop transmission is not supported by this chip revision");
    }
#else
    ESP_RETURN_ON_FALSE(config->flags.loop_transmission == false, ESP_ERR_NOT_SUPPORTED, TAG, "loop transmission is not supported on this chip");
#endif

#if !SOC_PARLIO_TX_SUPPORT_EOF_FROM_DMA
    // check the max payload size if it's not a loop transmission and the DMA EOF is not supported
    if (!config->flags.loop_transmission)
    {
        ESP_RETURN_ON_FALSE(tx_unit->max_transfer_bits <= PARLIO_LL_TX_MAX_BITS_PER_FRAME,
                            ESP_ERR_INVALID_ARG, TAG, "invalid transfer size, max transfer size should be less than %d", PARLIO_LL_TX_MAX_BITS_PER_FRAME / 8);
    }
#endif // !SOC_PARLIO_TX_SUPPORT_EOF_FROM_DMA

    size_t cache_line_size = 0;
    size_t alignment = 0;
    uint8_t cache_type = 0;
    esp_ptr_external_ram(payload) ? (alignment = tx_unit->ext_mem_align, cache_type = CACHE_LL_LEVEL_EXT_MEM) : (alignment = tx_unit->int_mem_align, cache_type = CACHE_LL_LEVEL_INT_MEM);
    // check alignment
    ESP_RETURN_ON_FALSE(((uint32_t)payload & (alignment - 1)) == 0, ESP_ERR_INVALID_ARG, TAG, "payload address not aligned");
    ESP_RETURN_ON_FALSE((payload_bits & (alignment - 1)) == 0, ESP_ERR_INVALID_ARG, TAG, "payload size not aligned");
    cache_line_size = cache_hal_get_cache_line_size(cache_type, CACHE_TYPE_DATA);

    if (cache_line_size > 0)
    {
        // Write back to cache to synchronize the cache before DMA start
        ESP_RETURN_ON_ERROR(esp_cache_msync((void *)payload, (payload_bits + 7) / 8,
                                            ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED),
                            TAG, "cache sync failed");
    }

    // check if to start a new transaction or update the current loop transaction
    bool no_trans_pending_in_queue = uxQueueMessagesWaiting(tx_unit->trans_queues[PARLIO_TX_QUEUE_PROGRESS]) == 0;
    if (tx_unit->cur_trans && tx_unit->cur_trans->flags.loop_transmission && config->flags.loop_transmission && no_trans_pending_in_queue)
    {
        tx_unit->cur_trans->payload = payload;
        tx_unit->cur_trans->payload_bits = payload_bits;
        parlio_mount_buffer(tx_unit, tx_unit->cur_trans);
        atomic_store(&tx_unit->buffer_need_switch, true);
    }
    else
    {
        TickType_t queue_wait_ticks = portMAX_DELAY;
        if (config->flags.queue_nonblocking)
        {
            queue_wait_ticks = 0;
        }
        parlio_tx_trans_desc_t *t = NULL;
        // acquire one transaction description from ready queue or complete queue
        if (xQueueReceive(tx_unit->trans_queues[PARLIO_TX_QUEUE_READY], &t, 0) != pdTRUE)
        {
            if (xQueueReceive(tx_unit->trans_queues[PARLIO_TX_QUEUE_COMPLETE], &t, queue_wait_ticks) == pdTRUE)
            {
                tx_unit->num_trans_inflight--;
            }
        }
        ESP_RETURN_ON_FALSE(t, ESP_ERR_INVALID_STATE, TAG, "no free transaction descriptor, please consider increasing trans_queue_depth");

        // fill in the transaction descriptor
        memset(t, 0, sizeof(parlio_tx_trans_desc_t));
        t->payload = payload;
        t->payload_bits = payload_bits;
        t->idle_value = config->idle_value & tx_unit->idle_value_mask;
        t->flags.loop_transmission = config->flags.loop_transmission;

        if (tx_unit->bs_handle)
        {
            t->bitscrambler_program = config->bitscrambler_program;
        }
        else if (config->bitscrambler_program)
        {
            ESP_RETURN_ON_ERROR(ESP_ERR_INVALID_STATE, TAG, "TX unit is not decorated with bitscrambler");
        }

        // send the transaction descriptor to progress queue
        ESP_RETURN_ON_FALSE(xQueueSend(tx_unit->trans_queues[PARLIO_TX_QUEUE_PROGRESS], &t, 0) == pdTRUE,
                            ESP_ERR_INVALID_STATE, TAG, "failed to send transaction descriptor to progress queue");
        tx_unit->num_trans_inflight++;

        // check if we need to start one pending transaction
        parlio_tx_fsm_t expected_fsm = PARLIO_TX_FSM_ENABLE;
        if (atomic_compare_exchange_strong(&tx_unit->fsm, &expected_fsm, PARLIO_TX_FSM_WAIT))
        {
            // check if we need to start one transaction
            if (xQueueReceive(tx_unit->trans_queues[PARLIO_TX_QUEUE_PROGRESS], &t, 0) == pdTRUE)
            {
                atomic_store(&tx_unit->fsm, PARLIO_TX_FSM_RUN);
                parlio_tx_queue_transaction(tx_unit, t);
            }
            else
            {
                atomic_store(&tx_unit->fsm, PARLIO_TX_FSM_ENABLE);
            }
        }
    }

    return ESP_OK;
}
