/**
 * @file ihex.c
 * @author Zach Peltzer
 * @date Created: Fri, 02 Feb 2018
 * @date Last Modified: Fri, 02 Feb 2018
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ihex.h"

/**
 * Begins a new block.
 * @param ih Intel hex writer state.
 * @param type Type of the new block.
 * @return Number of characters written.
 */
static void ihex_start_block(ihex_data *ih, ihex_block_type type);

/**
 * Ends the current block, writing the checksum and line break.
 * @param ih Intel hex writer state.
 * @return Number of characters written.
 */
static void ihex_finish_block(ihex_data *ih);

int ihex_data_init(ihex_data *ih, FILE *stream,
        uint8_t block_len, uint8_t page, uint16_t addr) {
    if (!ih || !stream) {
        return -1;
    }

    ih->block_data = malloc(block_len);
    if (!ih->block_data) {
        return -1;
    }

    ih->stream = stream;
    ih->block_len = block_len;

    ih->len = 0;
    ih->addr = 0x0000;
    ih->type = IH_NONE;

    ihex_set_page(ih, page, addr);

    return 0;
}

void ihex_finalize(ihex_data *ih) {
    if (!ih) {
        return;
    }

    /* TODO Does the address have to be set to 0? */
    ihex_set_addr(ih, 0x0000);
    ihex_start_block(ih, IH_END);
    ihex_finish_block(ih);

    free(ih->block_data);
}

/**
 * Writes a single byte to a stream in Intel hex format.
 * @param byte Byte to output.
 * byte written (i.e. incremented by it).
 * @param stream File stream to write to.
 */
void ihex_write_byte(ihex_data *ih, uint8_t byte) {
    if (!ih) {
        return;
    }

    if (ih->type == IH_NONE) {
        ihex_start_block(ih, IH_DATA);
    }

    ih->block_data[ih->len++] = byte;

    if (ih->len == ih->block_len) {
        ihex_finish_block(ih);
    }
}

void ihex_write_word(ihex_data *ih, uint16_t word) {
    ihex_write_byte(ih, (uint8_t) word);
    ihex_write_byte(ih, (uint8_t) (word >> 8));
}

void ihex_write_data(ihex_data *ih, const void *data, int size) {
    /* Just a cast to make the syntax easier below */
    const uint8_t *byte_data = (uint8_t *) data;

    for (int i = 0; i < size; i++) {
        ihex_write_byte(ih, byte_data[i]);
    }
}

void ihex_set_addr(ihex_data *ih, uint16_t addr) {
    ihex_finish_block(ih);
    ih->addr = addr;
}

void ihex_set_page(ihex_data *ih, uint8_t page, uint16_t addr) {
    ihex_set_addr(ih, 0x0000);
    ihex_start_block(ih, IH_PAGE);
    ihex_write_byte(ih, 0x00);
    ihex_write_byte(ih, page);
    ihex_finish_block(ih);

    ihex_set_addr(ih, addr);
}

static void ihex_start_block(ihex_data *ih, ihex_block_type type) {
    ihex_finish_block(ih);

    /* Length, checksum, and address are updated/reset in ihex_finish_block() */
    ih->type = type;
}

static void ihex_finish_block(ihex_data *ih) {
    uint8_t chksum;

    if (ih->type == IH_NONE) {
        return;
    }

    chksum = ih->len
        + (uint8_t) ih->addr + (uint8_t) (ih->addr >> 8)
        + ih->type;

    fprintf(ih->stream, ":%02X%04X%02X", ih->len, ih->addr, ih->type);
    for (int i = 0; i < ih->len; i++) {
        fprintf(ih->stream, "%02X", ih->block_data[i]);
        chksum += ih->block_data[i];
    }

    /* Have to re-cast the checksum after negation because of type promotion
     * when using variable-argument functions.
     */
    fprintf(ih->stream, "%02X\r\n", (uint8_t) -chksum);

    ih->addr += ih->len;
    ih->len = 0;
    ih->type = IH_NONE;
}

/* vim: set tw=80 ft=c: */
