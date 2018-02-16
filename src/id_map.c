/**
 * @file id_map.c
 * @author Zach Peltzer
 * @date Created: Thu, 08 Feb 2018
 * @date Last Modified: Thu, 08 Feb 2018
 */

#include <stdlib.h>

#include "id_map.h"

#define ID_MAP_INIT_CAP 4

int id_map_init(id_map *map) {
    if (!map) {
        return -1;
    }

    map->ids = malloc(sizeof(map->ids[0]) * ID_MAP_INIT_CAP);
    if (!map->ids) {
        return -1;
    }

    map->len = 0;
    map->cap = ID_MAP_INIT_CAP;
    return 0;
}

void id_map_destroy(id_map *map) {
    if (!map) {
        return;
    }

    free(map->ids);
}

int id_map_add(id_map *map, int key, int val) {
    if (!map) {
        return -1;
    }

    for (int i = 0; i < map->len; i++) {
        if (map->ids[i].key == key) {
            map->ids[i].val = val;
            return 0;
        }
    }

    if (map->len == map->cap) {
        map->cap *= 2;
        map->ids = realloc(map->ids, sizeof(map->ids[0]) * map->cap);
        if (!map->ids) {
            map->cap /= 2;
            return -1;
        }
    }

    map->ids[map->len].key = key;
    map->ids[map->len].val = val;
    map->len++;

    return 0;
}

int id_map_search(id_map *map, int key) {
    for (int i = 0; i < map->len; i++) {
        if (map->ids[i].key == key) {
            return map->ids[i].val;
        }
    }

    return -1;
}

/* vim: set tw=80 ft=c: */
