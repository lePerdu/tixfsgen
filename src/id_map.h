/**
 * @file id_map.h
 * @author Zach Peltzer
 * @date Created: Thu, 08 Feb 2018
 * @date Last Modified: Thu, 08 Feb 2018
 */

#ifndef ID_MAP_H_
#define ID_MAP_H_

typedef struct {
    int len;
    int cap;

    struct {
        int key;
        int val;
    } *ids;
} id_map;

int id_map_init(id_map *map);
void id_map_destroy(id_map *map);

int id_map_add(id_map *map, int key, int val);
int id_map_search(id_map *map, int key);

#endif /* ID_MAP_H_ */

/* vim: set tw=80 ft=c: */
