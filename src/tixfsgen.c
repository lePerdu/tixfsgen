/**
 * @file tixfsgen.c
 * @author Zach Peltzer
 * @date Created: Wed, 31 Jan 2018
 * @date Last Modified: Fri, 02 Feb 2018
 */

#include <dirent.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* TODO Make this less linux-specific */
#include <sys/sysmacros.h>

#include "ihex.h"

#define TIXFS_ANCHOR_START_PAGE 0x04
#define TIXFS_ANCHOR_END_PAGE 0x07

#define TIXFS_START_PAGE 0x08
/* TODO Set depending on model option */
#define TIXFS_END_PAGE 0x6B

#define TIXFS_REL_ADDR 0x4000

#define TIXFS_PAGE_SIZE 0x4000
#define TIXFS_FILE_SIZE_MAX (TIXFS_PAGE_SIZE - sizeof(tixfs_inode))

#define TIXFS_NAME_MAX 14

/* Only these filetypes are supported for now */
#define TIX_S_IFREG 0xC000
#define TIX_S_IFDIR 0xD000
#define TIX_S_IFCHR 0xA000
#define TIX_S_IFBLK 0x9000

#define TIX_S_INDFIL 0xF000

/**
 * Because the compiler can pack structures, these may not be equal to
 * sizeof(tixfs_inode), etc.
 */
#define TIXFS_SIZEOF_INODE 7
#define TIXFS_SIZEOF_DIR_ENTRY 16
#define TIXFS_SIZEOF_INODE_ENTRY 5

typedef struct {
    uint8_t page;
    uint16_t addr;
} tix_far_ptr;

typedef struct {
    uint16_t mode;
    uint16_t size;
    uint8_t uid;
    uint8_t gid;
    uint8_t nlinks;
} tixfs_inode;

typedef struct {
    uint16_t inode;
    char name[TIXFS_NAME_MAX];
} tixfs_dir_entry;

typedef struct {
    FILE *stream;
    ihex_data ih_writer;

    tix_far_ptr head;
    tix_far_ptr tail;

    int inode_cur;
    int inode_cap;
    tix_far_ptr *inodes;
} tixfs_data;


int tixfs_data_init(tixfs_data *fs, const char *file);
void tixfs_finalize(tixfs_data *fs);

void tixfs_write_inode(tixfs_data *fs,
        uint16_t inode_num, const tixfs_inode *inode);
void tixfs_write_file(tixfs_data *fs, uint16_t inode_num,
        const tixfs_inode *inode, const void *data);

uint16_t tixfs_add_file(tixfs_data *fs, const char *path);

int tixfs_data_init(tixfs_data *fs, const char *file) {
    if (!fs) {
        return -1;
    }

    fs->stream = fopen(file, "w");
    if (!fs->stream) {
        return -1;
    }

    if (ihex_data_init(&fs->ih_writer, fs->stream,
                32, TIXFS_START_PAGE, TIXFS_REL_ADDR) < 0) {
        fclose(fs->stream);
        return -1;
    }

    fs->head = (tix_far_ptr) {TIXFS_START_PAGE, TIXFS_REL_ADDR};
    fs->tail = (tix_far_ptr) {TIXFS_START_PAGE, TIXFS_REL_ADDR};

    fs->inode_cur = 1;
    fs->inode_cap = 16;
    fs->inodes = malloc(fs->inode_cap * sizeof(tix_far_ptr));
    if (!fs->inodes) {
        perror("Memory error");
        exit(EXIT_FAILURE);
    }

    return 0;
}

/**
 * Finalizes the filesystem by writing the inode file to the end of the output
 * file and writing
 */
void tixfs_finalize(tixfs_data *fs) {
    if (!fs) {
        return;
    }

    /* Write the inode file */

    tixfs_inode if_inode;

    if_inode.mode = TIX_S_INDFIL;
    if_inode.size = (fs->inode_cur - 1) * TIXFS_SIZEOF_INODE_ENTRY;
    if_inode.uid = 0;
    if_inode.gid = 0;
    if_inode.nlinks = 1;

    /* Write like a normal file with inode number 0. The data does not include
     * the first element (the inode file). */

    tixfs_write_inode(fs, 0, &if_inode);

    for (int inode = 1; inode < fs->inode_cur; inode++) {
        ihex_write_word(&fs->ih_writer, inode);
        ihex_write_byte(&fs->ih_writer, fs->inodes[inode].page);
        ihex_write_word(&fs->ih_writer, fs->inodes[inode].addr);
    }

    fs->tail.addr += if_inode.size;

    /* Fill the rest of the current page with 0xFF */
    ihex_write_fill(&fs->ih_writer, 0xFF,
            TIXFS_REL_ADDR + TIXFS_PAGE_SIZE - fs->tail.addr);

    /* Fill the rest of the current block with 0xFF */
    for (int p = fs->tail.page+1; p % 4 > 0; p++) {
        ihex_set_page(&fs->ih_writer, p, TIXFS_REL_ADDR);
        ihex_write_fill(&fs->ih_writer, 0xFF, TIXFS_PAGE_SIZE);
    }

    /* TODO Write the anchor data */
    /* Head of the filesystem = start of first page */
    ihex_set_page(&fs->ih_writer, TIXFS_ANCHOR_START_PAGE, TIXFS_REL_ADDR);
    ihex_write_byte(&fs->ih_writer, TIXFS_START_PAGE);
    ihex_write_word(&fs->ih_writer, TIXFS_REL_ADDR);
    /* Fill the rest of the page with 0xFF */
    ihex_write_fill(&fs->ih_writer, 0xFF, TIXFS_PAGE_SIZE - 3);

    /* Fill middle pages with 0xFF */
    ihex_set_page(&fs->ih_writer, TIXFS_ANCHOR_START_PAGE + 1, TIXFS_REL_ADDR);
    ihex_write_fill(&fs->ih_writer, 0xFF, TIXFS_PAGE_SIZE);
    ihex_set_page(&fs->ih_writer, TIXFS_ANCHOR_START_PAGE + 2, TIXFS_REL_ADDR);
    ihex_write_fill(&fs->ih_writer, 0xFF, TIXFS_PAGE_SIZE);

    /* Fill last page with 0xFF and the inode file location */
    ihex_set_page(&fs->ih_writer, TIXFS_ANCHOR_START_PAGE + 3, TIXFS_REL_ADDR);
    ihex_write_fill(&fs->ih_writer, 0xFF, TIXFS_PAGE_SIZE - 4);
    /* Inode file location was stored in the inode array */
    ihex_write_byte(&fs->ih_writer, fs->inodes[0].page);
    ihex_write_word(&fs->ih_writer, fs->inodes[0].addr);

    /* Free data */

    ihex_finalize(&fs->ih_writer);
    fclose(fs->stream);
    free(fs->inodes);
}

void tixfs_write_inode(tixfs_data *fs,
        uint16_t inode_num, const tixfs_inode *inode) {
    uint16_t remaining = TIXFS_REL_ADDR + TIXFS_PAGE_SIZE - fs->tail.addr;

    /* Adjust the offset and fill with 1s ($FF) if the file would extend past a
     * page boundary
     */
    if (remaining < TIXFS_SIZEOF_INODE + inode->size) {
        /* Fill with 0xFF to the end of the page */
        ihex_write_fill(&fs->ih_writer, 0xFF, remaining);

        fs->tail.addr = TIXFS_REL_ADDR;
        fs->tail.page++;
        if (fs->tail.page > TIXFS_END_PAGE) {
            fprintf(stderr, "Error: Filesystem full.");
            return;
        }

        /* Only write a page block when the page changes */
        ihex_set_page(&fs->ih_writer, fs->tail.page, fs->tail.addr);
    }

    /* Write the inode. Since the compiler can align structure fields, they are
     * written manually
     */
    ihex_write_word(&fs->ih_writer, inode->mode);
    ihex_write_word(&fs->ih_writer, inode->size);
    ihex_write_byte(&fs->ih_writer, inode->uid);
    ihex_write_byte(&fs->ih_writer, inode->gid);
    ihex_write_byte(&fs->ih_writer, inode->nlinks);

    /* Store the addresses relocated to memory bank A */
    fs->inodes[inode_num] = fs->tail;

    fs->tail.addr += TIXFS_SIZEOF_INODE;
}

/**
 * Writes data to the output file.
 */
void tixfs_write_file(tixfs_data *fs,
        uint16_t inode_num,
        const tixfs_inode *inode,
        const void *data) {
    if (!fs) {
        return;
    }

    /* Moves to the next page if necessary */
    tixfs_write_inode(fs, inode_num, inode);

    /* Write the data */
    ihex_write_data(&fs->ih_writer, data, inode->size);

    fs->tail.addr += inode->size;
}

/**
 * Recursively adds files to the filesystem, writing them to the output file.
 */
uint16_t tixfs_add_file(tixfs_data *fs, const char *path) {
    FILE *file_stream;
    DIR *dir;
    struct dirent *dentry;
    struct stat file_stat;

    tixfs_inode t_inode;

    uint16_t inode_num;

    int buf_idx;
    int buf_size;
    uint8_t *buf;

    if (!fs) {
        return 0;
    }

    /* Reserve inode first (mainly so that the root directory will have an inode
     * number of 1). Grow the buffer if necessary.
     */
    if (fs->inode_cur >= fs->inode_cap) {
        fs->inode_cap *= 2;
        fs->inodes = realloc(fs->inodes, fs->inode_cap);
        if (!fs->inodes) {
            perror("Memory error");
            exit(EXIT_FAILURE);
        }
    }

    inode_num = fs->inode_cur++;

    if (stat(path, &file_stat) < 0) {
        return 0;
    }

    /* Copy the UIDs and GIDs.
     * Since the size of the values is likely larger on this system than in
     * TIX, they are truncated to single-byte.
     */
    t_inode.uid = file_stat.st_uid;
    t_inode.gid = file_stat.st_gid;

    /* TODO Verify that all files linking to this file are in the sub-directory,
     * because otherwise an inode could never be freed within TIX
     */
    t_inode.nlinks = file_stat.st_nlink;

    t_inode.mode = file_stat.st_mode & 07777; /* Permission bits */

    if (S_ISREG(file_stat.st_mode)) {
        t_inode.mode |= TIX_S_IFREG;

        /* For regular files, write the inode and data right now */

        if (file_stat.st_size > TIXFS_FILE_SIZE_MAX) {
            fprintf(stderr,
                    "Warning: Size of file \"%s\" is larger than the maximum "
                    "file size (%ld). The file will be truncated.\n",
                    path, TIXFS_FILE_SIZE_MAX);
        }
        t_inode.size = file_stat.st_size;

        file_stream = fopen(path, "r");
        if (!file_stream) {
            fprintf(stderr,
                    "Warning: File \"%s\" cannot be opened for reading. "
                    "Skipping.\n",
                    path);
            return 0;
        }

        /* Read the file into a buffer first */
        buf = malloc(t_inode.size);
        if (!buf) {
            fclose(file_stream);
            perror("Memory error");
            exit(EXIT_FAILURE);
        }

        fread(buf, 1, t_inode.size, file_stream);
        tixfs_write_file(fs, inode_num, &t_inode, buf);

        fclose(file_stream);
        free(buf);

    } else if (S_ISDIR(file_stat.st_mode)) {
        t_inode.mode |= TIX_S_IFDIR;

        /* Hard links are not supported for directories, but there is always a
         * ".." entry in each directory
         */
        t_inode.nlinks = 2;

        /* For directories, recursively add children.
         * The children are written to the output file first sincne we have to
         * calculate the size of this file by looking at all files in it.
         */

        dir = opendir(path);
        if (!dir) {
            return 0;
        }

        buf_idx = 0;
        buf_size = 4 * sizeof(tixfs_dir_entry);
        buf = malloc(buf_size);
        if (!buf) {
            perror("Memor error");
            exit(EXIT_FAILURE);
        }

        /* Write the ".." entry */
        buf[buf_idx++] = inode_num & 0xFF;
        buf[buf_idx++] = (inode_num >> 8) & 0xFF;
        strcpy(&buf[buf_idx], "..");
        buf_idx += TIXFS_NAME_MAX;

        t_inode.size = sizeof(tixfs_dir_entry);

        while ((dentry = readdir(dir))) {
            /* The ".." entry was added above since whether or not it will show
             * up in readdir() is implementation-dependent
             */
            if (strcmp(dentry->d_name, ".") == 0
                    || strcmp(dentry->d_name, "..") == 0) {
                continue;
            }

            /* TODO Use some sort of relative path instead of constructing a new
             * path each time.
             */
            char *ent_path = malloc(strlen(path) + strlen(dentry->d_name) + 2);
            if (!ent_path) {
                perror("Memor error");
                exit(EXIT_FAILURE);
            }

            ent_path[0] = 0;
            strcat(ent_path, path);
            strcat(ent_path, "/");
            strcat(ent_path, dentry->d_name);

            uint16_t ent_inode = tixfs_add_file(fs, ent_path);
            if (!ent_inode) {
                /* Something went wrong, so skip the file */
                continue;
            }

            buf[buf_idx++] = ent_inode & 0xFF;
            buf[buf_idx++] = (ent_inode >> 8) & 0xFF;
            strncpy(&buf[buf_idx], dentry->d_name, TIXFS_NAME_MAX);
            buf_idx += TIXFS_NAME_MAX;

            /* TODO This and buf_idx are always going to be the same value */
            t_inode.size += sizeof(tixfs_dir_entry);

            if (buf_idx >= buf_size) { 
                buf_size *= 2;
                buf = realloc(buf, buf_size);
                if (!buf) {
                    perror("Memor error");
                    exit(EXIT_FAILURE);
                }
            }

            free(ent_path);
        }

        tixfs_write_file(fs, inode_num, &t_inode, buf);

        free(buf);

    } else if (S_ISCHR(file_stat.st_mode) || S_ISBLK(file_stat.st_mode)) {
        if (S_ISCHR(file_stat.st_mode)) {
            t_inode.mode |= TIX_S_IFCHR;
        } else {
            t_inode.mode |= TIX_S_IFBLK;
        }

        /* Get the major and minor device IDs.
         * TODO Find a less linux-specific way to do this
         */
        uint8_t dev_id[2];
        dev_id[0] = major(file_stat.st_rdev);
        dev_id[1] = minor(file_stat.st_rdev);

        t_inode.size = 2;

        tixfs_write_file(fs, inode_num, &t_inode, dev_id);
    } else {
        fprintf(stderr,
                "Warning: Type of file \"%s\" is not supported. The file will "
                "be ignored.\n",
                path);
    }

    return inode_num;
}

void usage(const char *exec_name) {
    printf("usage: %s <outfile> <directory>\n", exec_name);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    /* TODO 
     * Add option for model (to get page limits).
     * Add option (and recognize filename) to output in either raw binary or
     * Intel hex format.
     */

    tixfs_data fs;

    if (tixfs_data_init(&fs, argv[1]) < 0) {
        return -1;
    }

    tixfs_add_file(&fs, argv[2]);
    tixfs_finalize(&fs);

    return EXIT_SUCCESS;
}

/* vim: set tw=80 ft=c: */
