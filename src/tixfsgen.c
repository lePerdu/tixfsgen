/**
 * @file tixfsgen.c
 * @author Zach Peltzer
 * @date Created: Wed, 31 Jan 2018
 * @date Last Modified: Thu, 22 Feb 2018
 */

#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* TODO Make this less linux-specific */
#include <sys/sysmacros.h>
#include <sys/stat.h>

#include "id_map.h"
#include "ihex.h"

#define TIXFS_START_PAGE 0x04
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

    uint8_t start_page, end_page;

    tix_far_ptr head, tail;

    int inode_cur;
    int inode_cap;
    tix_far_ptr *inodes;
} tixfs_data;

static id_map uid_map;
static id_map gid_map;
static id_map dev_min_map;
static id_map dev_maj_map;

static int tixfs_data_init(tixfs_data *fs, uint8_t start_page, uint8_t end_page,
        FILE *stream);
static void tixfs_finalize(tixfs_data *fs);

static void tixfs_write_inode(tixfs_data *fs,
        uint16_t inode_num, const tixfs_inode *inode);
static void tixfs_write_file(tixfs_data *fs, uint16_t inode_num,
        const tixfs_inode *inode, const void *data);

static uint16_t tixfs_add_file(tixfs_data *fs,
        uint16_t pinode_num, tixfs_inode *pinode, const char *path);

static void usage(const char *exec_name);

int tixfs_data_init(tixfs_data *fs, uint8_t start_page, uint8_t end_page,
        FILE *stream) {
    if (!fs) {
        return -1;
    }

    fs->start_page = start_page;
    fs->end_page = end_page; /* TODO Actually use this value */
    fs->stream = stream;

    if (ihex_data_init(&fs->ih_writer, fs->stream,
                32, start_page + 4, TIXFS_REL_ADDR) < 0) {
        fclose(fs->stream);
        return -1;
    }

    /* 1 block (4 pages) is reserved as the anchor block */
    fs->head = (tix_far_ptr) {start_page + 4, TIXFS_REL_ADDR};
    fs->tail = (tix_far_ptr) {start_page + 4, TIXFS_REL_ADDR};

    fs->inode_cur = 1;
    fs->inode_cap = 16;
    fs->inodes = malloc(fs->inode_cap * sizeof(fs->inodes[0]));
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
    if_inode.nlinks = 0;

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

    /* Head of the filesystem = start of first page */
    ihex_set_page(&fs->ih_writer, fs->start_page, TIXFS_REL_ADDR);
    ihex_write_byte(&fs->ih_writer, TIXFS_START_PAGE);
    ihex_write_word(&fs->ih_writer, TIXFS_REL_ADDR);
    /* Fill the rest of the page with 0xFF */
    ihex_write_fill(&fs->ih_writer, 0xFF, TIXFS_PAGE_SIZE - 3);

    /* Fill middle pages with 0xFF */
    ihex_set_page(&fs->ih_writer, fs->start_page + 1, TIXFS_REL_ADDR);
    ihex_write_fill(&fs->ih_writer, 0xFF, TIXFS_PAGE_SIZE);
    ihex_set_page(&fs->ih_writer, fs->start_page + 2, TIXFS_REL_ADDR);
    ihex_write_fill(&fs->ih_writer, 0xFF, TIXFS_PAGE_SIZE);

    /* Fill last page with 0xFF and the inode file location */
    ihex_set_page(&fs->ih_writer, fs->start_page + 3, TIXFS_REL_ADDR);
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
 * @param fs Filesystem data.
 * @param pinode_num Inode number of the parent directory in the TIX filesystem.
 * @param pinode Inode data of the parant directory TODO Don't pass this
 * directly.
 * @param path Path of the file in the local filesystem.
 */
uint16_t tixfs_add_file(tixfs_data *fs,
        uint16_t pinode_num, tixfs_inode *pinode, const char *path) {
    FILE *file_stream;
    DIR *dir;
    struct dirent *dentry;
    struct stat file_stat;
    int id;

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
        fs->inodes = realloc(fs->inodes, fs->inode_cap * sizeof(fs->inodes[0]));
        if (!fs->inodes) {
            perror("Memory error");
            exit(EXIT_FAILURE);
        }
    }

    inode_num = fs->inode_cur++;

    /* pinode_num == 0 means that this should be the root inode */
    if (pinode_num == 0) {
        pinode_num = inode_num;
        pinode = &t_inode;
    }

    if (stat(path, &file_stat) < 0) {
        return 0;
    }

    /* Copy the UIDs and GIDs.
     * Since the size of the values is likely larger on this system than in
     * TIX, they are truncated to single-byte.
     */
    if ((id = id_map_search(&uid_map, file_stat.st_uid)) != -1) {
        t_inode.uid = id;
    } else {
        t_inode.uid = file_stat.st_uid;
    }
    if ((id = id_map_search(&gid_map, file_stat.st_gid)) != -1) {
        t_inode.gid = id;
    } else {
        t_inode.gid = file_stat.st_gid;
    }

    /* TODO Verify that all files linking to this file are in the sub-directory,
     * because otherwise an inode could never be freed within TIX. Currently,
     * hard linked files are just copied.
     */
    t_inode.nlinks = 1;

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

        /* Write the ".." entry. */
        buf[buf_idx++] = pinode_num & 0xFF;
        buf[buf_idx++] = (pinode_num >> 8) & 0xFF;
        strncpy(&buf[buf_idx], "..", TIXFS_NAME_MAX);
        buf_idx += TIXFS_NAME_MAX;
        /* Add a link to the parent */
        pinode->nlinks++;

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

            uint16_t ent_inode = tixfs_add_file(fs,
                    inode_num, &t_inode, ent_path);
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
        closedir(dir);

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

        if ((id = id_map_search(&dev_maj_map, dev_id[0])) != -1) {
            dev_id[0] = id;
        }

        if ((id = id_map_search(&dev_min_map, dev_id[1])) != -1) {
            dev_id[1] = id;
        }

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
    printf(
"tixfsgen v0.0 by Zach Peltzer\n"
"usage: %1$s [OPTION]... <OUTFILE> <DIRECTORY>\n"
"   or: %1$s [OPTION]... -r <OUTFILE> <FILE>...\n"
"Create a TIXFS filesystem from a specified root directory or files from a\n"
"list of files to be put at the root.\n\n"
"options:\n"
"  -r               put specified files into the root director instead of\n"
"                     using a specified root directory\n"
"  -m<model>        model of the calculator to output for. This determines\n"
"                     amount of flash ROM available\n"
"  -p<page>         page to start the filesystem. This is 0x04 by default\n"
"  -e<page>         last page available to the filesystem. The default and\n"
"                     maximum value are determined by the model\n"
"  -u<host>:<tix>   replace the UID <host> with <tix> in the TIXFS filesystem\n"
"  -g<host>:<tix>   replace the GID <host> with <tix> in the TIXFS filesystem\n"
"  -d<host>:<tix>   replace the minor device number <host> with <tix> in the\n"
"                     TIXFS filesystem\n"
"  -D<host>:<tix>   replace the major device number <host> with <tix> in the\n"
"                     TIXFS filesystem\n"
            ,exec_name);
}

int main(int argc, char *argv[]) {
    tixfs_data fs;
    const char *in_filename, *out_filename;
    FILE *out_file;
    int opt;
    int create_root = 0;
    int start_page = TIXFS_START_PAGE, end_page = TIXFS_END_PAGE;

    char *end_ptr; /** Used in strtol() */
    int tmp;
    struct passwd *user;
    struct group *group;
    int host_id, tix_id;

    id_map_init(&uid_map);
    id_map_init(&gid_map);
    id_map_init(&dev_min_map);
    id_map_init(&dev_maj_map);

    while ((opt = getopt(argc, argv, ":m:p:e:u:g:d:D:rh")) != -1) {
        switch (opt) {
        case 'p':
            tmp = strtol(optarg, &end_ptr, 0);
            if (end_ptr == optarg || *end_ptr != 0 /* More characters left */
                    || tmp > 0xFF || 0 > tmp) {
                fprintf(stderr,
                        "Error: Page must be a positive 8-bit integer\n");
                return EXIT_FAILURE;
            }

            start_page = tmp;
            break;

        case 'e':
            tmp = strtol(optarg, &end_ptr, 0);
            if (end_ptr == optarg || *end_ptr != 0 /* More characters left */
                    || tmp > 0xFF || 0 > tmp) {
                fprintf(stderr,
                        "Error: Page must be a positive 8-bit integer\n");
                return EXIT_FAILURE;
            }

            end_page = tmp;
            break;

        case 'u':
            host_id = strtol(optarg, &end_ptr, 0);
            if (end_ptr == optarg || *end_ptr != ':') {
                end_ptr = strchr(optarg, ':');
                *end_ptr = 0; /* Temporarily modify the argument */
                user = getpwnam(optarg);
                if (!user) {
                    fprintf(stderr, "Warning: Invalid user: %s\n", optarg);
                    break;
                }
                *end_ptr = ':';

                host_id = user->pw_uid;
            } else {
                user = getpwuid(host_id);
                if (!user) {
                    fprintf(stderr, "Warning: Invalid UID: %d\n", host_id);
                    break;
                }
            }
            optarg = end_ptr + 1; /* Move past semicolon */

            tix_id = strtol(optarg, &end_ptr, 0);
            if (end_ptr == optarg || *end_ptr != 0
                    || tix_id < 0 || tix_id > 0xFF) {
                fprintf(stderr, "Warning: Invalid mapping: %s\n", optarg);
                break;
            }

            id_map_add(&uid_map, host_id, tix_id);
            break;

        case 'g':
            host_id = strtol(optarg, &end_ptr, 0);
            if (end_ptr == optarg || *end_ptr != ':') {
                end_ptr = strchr(optarg, ':');
                *end_ptr = 0; /* Temporarily modify the argument */
                group = getgrnam(optarg);
                if (!group) {
                    fprintf(stderr, "Warning: Invalid group: %s\n", optarg);
                    break;
                }
                *end_ptr = ':';

                host_id = group->gr_gid;
            } else {
                group = getgrgid(host_id);
                if (!group) {
                    fprintf(stderr, "Warning: Invalid GID: %d\n", host_id);
                    break;
                }
            }

            optarg = end_ptr + 1; /* Move past semicolon */

            tix_id = strtol(optarg, &end_ptr, 0);
            if (end_ptr == optarg || *end_ptr != 0
                    || tix_id < 0 || tix_id > 0xFF) {
                fprintf(stderr, "Warning: Invalid mapping: %s\n", optarg);
                break;
            }

            id_map_add(&gid_map, host_id, tix_id);
            break;

        case 'd':
            host_id = strtol(optarg, &end_ptr, 0);
            if (end_ptr == optarg || *end_ptr != 0
                    || tix_id < 0 || tix_id > 0xFF) {
                fprintf(stderr, "Warning: Invalid mapping: %s\n", optarg);
                break;
            }
            optarg = end_ptr + 1; /* Move past semicolon */

            tix_id = strtol(optarg, &end_ptr, 0);
            if (end_ptr == optarg || *end_ptr != 0
                    || tix_id < 0 || tix_id > 0xFF) {
                fprintf(stderr, "Warning: Invalid mapping: %s\n", optarg);
                break;
            }

            /* TODO Mapping minor IDs is pretty meaningless except in the
             * context of a major ID
             */
            id_map_add(&dev_min_map, host_id, tix_id);
            break;

        case 'D':
            host_id = strtol(optarg, &end_ptr, 0);
            if (end_ptr == optarg || *end_ptr != 0
                    || tix_id < 0 || tix_id > 0xFF) {
                fprintf(stderr, "Warning: Invalid mapping: %s\n", optarg);
                break;
            }
            optarg = end_ptr + 1; /* Move past semicolon */

            tix_id = strtol(optarg, &end_ptr, 0);
            if (end_ptr == optarg || *end_ptr != 0
                    || tix_id < 0 || tix_id > 0xFF) {
                fprintf(stderr, "Warning: Invalid mapping: %s\n", optarg);
                break;
            }

            id_map_add(&dev_maj_map, host_id, tix_id);
            break;

        case 'r':
        case 'm':
            fprintf(stderr, "Error: Unimplemented option: %c\n", opt);
            return EXIT_FAILURE;
        case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
        case ':':
            fprintf(stderr, "Error: Argument required for option: %c\n", opt);
            return EXIT_FAILURE;
        case '?':
            if (optopt == '-') {
                /* "--" stops options */
                /* TODO Parse long options ? */
                optind++;
                goto GETOPT_END;
            }

            fprintf(stderr, "Error: Unknown option: -%c\n", opt);
            break;
        }
    }
GETOPT_END:

    if (optind >= argc) {
        fprintf(stderr, "Error: No output file specified.\n");
        return EXIT_FAILURE;
    }

    out_filename = argv[optind++];
    out_file = fopen(out_filename, "w");
    if (!out_file) {
        fprintf(stderr, "Error: Could not open file %s\n", out_filename);
        return EXIT_FAILURE;
    }

    if (tixfs_data_init(&fs, start_page, end_page, out_file) < 0) {
        return -1;
    }

    if (create_root) {

    } else {
        if (argc - 1 > optind) {
            fprintf(stderr,
                    "Warning: Multiple input files specified without -r\n");
        }

        /* 0 indicates the root inode should be used (even though its inode
         * number is 1)
         */
        tixfs_add_file(&fs, 0, NULL, argv[optind]);
    }

    tixfs_finalize(&fs);

    id_map_destroy(&uid_map);
    id_map_destroy(&gid_map);
    id_map_destroy(&dev_min_map);
    id_map_destroy(&dev_maj_map);

    return EXIT_SUCCESS;
}

/* vim: set tw=80 ft=c: */
