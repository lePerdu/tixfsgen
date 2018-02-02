/**
 * @file ihex.h
 * @author Zach Peltzer
 * @date Created: Fri, 02 Feb 2018
 * @date Last Modified: Fri, 02 Feb 2018
 */

#ifndef IHEX_H_
#define IHEX_H_

#include <stdint.h>
#include <stdio.h>

typedef enum ihex_block_type {
    IH_NONE = -1,
    IH_DATA = 0,
    IH_END  = 1,
    IH_PAGE = 2,
} ihex_block_type;

/**
 * Stores the state of writing in Intel hex format.
 */
typedef struct ihex_data {
    /**
     * Stream to write to.
     */
    FILE *stream;

    /**
     * Maximum number of bytes in a block.
     */
    uint8_t block_len;

    /**
     * Number of bytes in the current block.
     */
    uint8_t len;

    /**
     * Starting address of the current block.
     */
    uint16_t addr;

    /**
     * Type of the current block.
     */
    ihex_block_type type;

    /**
     * Since we cannot know how much data will be in each block, it is buffered
     * and the block written all at once.
     * This will be allocated to be block_len bytes in size.
     */
    uint8_t *block_data;
} ihex_data;

/**
 * Initializes an Intel hex format writer.
 * @param ih Intel hex writer state.
 * @param stream Stream to write to.
 * @param block_len Maximum number of bytes in each block.
 * @param addr Starting address to output. This can be changed later via
 * ihex_set_addr().
 */
int ihex_data_init(ihex_data *ih, FILE *stream,
        uint8_t block_len, uint8_t page, uint16_t addr);

/**
 * Finalizes the Intel hex data and frees data from an Intel hex writer.
 * The current block is finalized and the end block is written.
 * No writes using this writer should occur after this until the next call to
 * ihex_data_init().
 * @param ih Intel hex writer state to finalize.
 */
void ihex_finalize(ihex_data *ih);

/**
 * Writes a single byte to a stream in Intel hex format.
 * @param ih Intel hex writer state.
 * @param byte Byte to output.
 */
void ihex_write_byte(ihex_data *ih, uint8_t byte);

/**
 * Writes a 16-bit word to a stream in little-endian Intel hex format.
 * @param ih Intel hex writer state.
 * @param word Word to write.
 */
void ihex_write_word(ihex_data *ih, uint16_t word);

/**
 * Writes binary data to a stream in Intel hex format.
 * @param ih Intel hex writer state.
 * @param data Data to write.
 * @param size Number of bytes to write.
 */
void ihex_write_data(ihex_data *ih, const void *data, int size);

/**
 * Writes the same byte multiple times to a stream in Intel hex format.
 * @param ih Intel hex writer state.
 * @param valueu Byte to fill with.
 * @param size Number of bytes to write.
 */
void ihex_write_fill(ihex_data *ih, uint8_t value, int size);

/**
 * Gets the starting output address of the current block
 * @param ih Intel hex writer state to read from.
 * @return Starting address of the current block.
 *
 * TODO Remove?
 */
/* uint16_t ihex_get_addr(const ihex_data *ih); */

/**
 * Changes the output address to write to.
 * This finishes the current block; a new one will start at the next write.
 * @param ih Intel hex writer state.
 * @param addr New address to set.
 */
void ihex_set_addr(ihex_data *ih, uint16_t addr);

/**
 * Changes the output page and address to write to.
 * This finishes the current block and writes a page block.
 * @param ih Intel hex writer state.
 * @param page New page to set.
 * @param addr Starting address for this page. This are set at the same time as
 * the page since it would generally be an error to continue at the same address
 * on a different page.
 */
void ihex_set_page(ihex_data *ih, uint8_t page, uint16_t addr);

#endif /* IHEX_H_ */

/* vim: set tw=80 ft=c: */
