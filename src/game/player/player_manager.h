#ifndef PLAYER_MANAGER_H
#define PLAYER_MANAGER_H

typedef struct list_s List;
typedef struct dyad_Stream dyad_Stream;
typedef struct player_s player;

struct player_manager {
    List *players;
};

void player_manager_init();
player *player_manager_add(dyad_Stream*);
void player_manager_remove(dyad_Stream*);
player *player_manager_find(dyad_Stream*);

#endif