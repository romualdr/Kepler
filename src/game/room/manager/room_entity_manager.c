#include <stdlib.h>
#include <stdio.h>

#include "log.h"
#include "list.h"
#include "hashtable.h"
#include "thpool.h"

#include "database/queries/rooms/room_vote_query.h"

#include "game/pathfinder/coord.h"
#include "game/player/player.h"

#include "game/items/item.h"

#include "game/room/room.h"
#include "game/room/room_user.h"
#include "game/room/room_task.h"

#include "game/room/mapping/room_map.h"
#include "game/room/mapping/room_model.h"
#include "game/room/mapping/room_tile.h"

#include "game/room/manager/room_entity_manager.h"

#include "util/stringbuilder.h"
#include "util/threading.h"

#include "communication/messages/outgoing_message.h"

/**
 * Get a unused instance id for the room they're in.
 *
 * @param room_user the room user struct to assign to
 * @return the new instance id
 */
int create_instance_id(room_user *room_user) {
    int instance_id = 0;

    while (get_room_user_by_instance_id((room*) room_user->room, instance_id) != NULL) {
        instance_id++;
    }

    return instance_id;
}

/**
 * Find a room user by instance id.
 *
 * @param room the room to search in
 * @param instance_id the instance id to search for
 * @return the room user struct
 */
room_user *get_room_user_by_instance_id(room *room, int instance_id) {
    for (size_t i = 0; i < list_size(room->users); i++) {
        session *room_player;
        list_get_at(room->users, i, (void *) &room_player);

        if (room_player->room_user->instance_id == instance_id) {
            return (room_user*) room_player->room_user;
        }
    }

    return NULL;
}

/**
 * Room entry handler.
 *
 * @param om the outgoing message
 * @param player the player
 */
void room_enter(room *room, session *player) {
    // Leave other room
    if (player->room_user->room != NULL) {
        room_leave(player->room_user->room, player, false);
    }

    if (list_size(room->users) == 0) {
        room_load_data(room);
    }

    if (room->room_data->model_data == NULL) {
        log_debug("Room %i has invalid model data.", room->room_data->id);
        return;
    }

    room_user *room_entity = player->room_user;

    room_entity->room = room;
    room_entity->room_id = room->room_id;
    room_entity->instance_id = create_instance_id(room_entity);
    room_user_reset_idle_timer(player->room_user);

    room_entity->position->x = room->room_data->model_data->door_x;
    room_entity->position->y = room->room_data->model_data->door_y;
    room_entity->position->z = room->room_data->model_data->door_z;

    coord_set_rotation(room_entity->position,
                       room->room_data->model_data->door_dir,
                       room->room_data->model_data->door_dir);

    list_add(room->users, player);
    room->room_data->visitors_now = (int) list_size(room->users);

    if (room->room_schedule_job == NULL) {
        room->room_schedule_job = create_runnable();
        room->room_schedule_job->request = room_task;
        room->room_schedule_job->room_id = room->room_id;
        room->room_schedule_job->millis = 500;
        thpool_add_work(global.thread_manager.pool, (void *) do_room_task, room->room_schedule_job);
    }

    /*outgoing_message *om = om_create(73); // "AI"
    player_send(session, om);
    om_cleanup(om);*/

    outgoing_message *om = om_create(166); // "Bf"
    om_write_str(om, "/client/");
    player_send(player, om);
    om_cleanup(om);

    om = om_create(69); // "AE"
    sb_add_string(om->sb, room->room_data->model);
    sb_add_string(om->sb, " ");
    sb_add_int(om->sb, room->room_id);
    player_send(player, om);
    om_cleanup(om);

    if (room->room_data->wallpaper > 0) {
        om = om_create(46); // "@n"
        sb_add_string(om->sb, "wallpaper/");
        sb_add_int(om->sb, room->room_data->wallpaper);
        player_send(player, om);
        om_cleanup(om);
    }

    if (room->room_data->floor > 0) {
        om = om_create(46); // "@n"
        sb_add_string(om->sb, "floor/");
        sb_add_int(om->sb, room->room_data->floor);
        player_send(player, om);
        om_cleanup(om);
    }

    // TODO: move votes to rooms object and load on initialization to reduce query load
    // Check if already voted, return if voted
    int voted = room_query_check_voted(room->room_data->id, player->player_data->id);
    int vote_count = -1;

    // If user already has voted, we sent total vote count
    // else we sent -1, making the vote selector pop up
    if (voted != -1) {
        vote_count = room_query_count_votes(room->room_data->id);
    }

    om = om_create(345); // "EY"
    om_write_int(om, vote_count);
    player_send(player, om);
    om_cleanup(om);

    // Show new session current state of an item program for pools
    if (list_size(room->room_data->model_data->public_items) > 0) {
        for (size_t i = 0; i < list_size(room->items); i++) {
            item *item;
            list_get_at(room->items, i, (void *) &item);

            if (!item->definition->behaviour->is_public_space_object) {
                continue;
            }

            if (item->current_program != NULL &&
                (strcmp(item->current_program, "curtains1") == 0
                 || strcmp(item->current_program, "curtains2") == 0
                 || strcmp(item->current_program, "door") == 0)) {

                om = om_create(71); // "AG"
                sb_add_string(om->sb, item->current_program);

                if (item->current_program_state != NULL && strlen(item->current_program_state) > 0) {
                    sb_add_string(om->sb, " ");
                    om_write_str(om, item->current_program_state);
                }

                player_send(player, om);
                om_cleanup(om);
            }
        }
    }


    player->room_user->authenticate_id = -1;

    if (strcmp(player->room_user->room->room_data->model_data->model_name, "park_b") == 0) {
        om = om_create(79); // "AO"
        sb_add_string_delimeter(om->sb, "Is Sojobo a faggot?", 13);

        sb_add_int_delimeter(om->sb, 1, ':');
        sb_add_string_delimeter(om->sb, "Yes", ':');
        sb_add_char(om->sb, 13);

        sb_add_int_delimeter(om->sb, 2, ':');
        sb_add_string_delimeter(om->sb, "A massive faggot", ':');
        sb_add_char(om->sb, 13);

        sb_add_char(om->sb, 2);
        player_send(player, om);
        om_cleanup(om);
    }
}

/**
 * Leave room handler, will make room and id for the room user reset back to NULL and 0.
 * And remove the character from the room.
 */
void room_leave(room *room, session *player, bool hotel_view) {
    if (!list_contains(room->users, player)) {
        return;
    }

    list_remove(room->users, player, NULL);
    room->room_data->visitors_now = (int) list_size(room->users);

    // Remove current user from tile
    room_tile *current_tile = room->room_map->map[player->room_user->position->x][player->room_user->position->y];
    current_tile->entity = NULL;

    // Reset item program state for pool items
    item *item = current_tile->highest_item;
    if (item != NULL) {
        if (item->current_program != NULL &&
            (strcmp(item->current_program, "curtains1") == 0
             || strcmp(item->current_program, "curtains2") == 0
             || strcmp(item->current_program, "door") == 0)) {

            item_assign_program(item, "open");
        }
    }

    outgoing_message *om;

    // Make figure vanish from the rooms
    om = om_create(29); // "@]"
    sb_add_int(om->sb, player->room_user->instance_id);
    room_send(room, om);
    om_cleanup(om);

    // Reset rooms user
    room_user_reset(player->room_user);
    room_dispose(room, false);

    // Go to hotel view, if told so.
    if (hotel_view) {
        om = om_create(18); // "@R"
        player_send(player, om);
        om_cleanup(om);
    }
}

/**
 * Append user list to the packet.
 *
 * @param om the outgoing message
 * @param player the player
 */
void append_user_list(outgoing_message *players, session *player) {
    char user_id[11], instance_id[11];
    sprintf(user_id, "%i", player->player_data->id);
    sprintf(instance_id, "%i", player->room_user->instance_id);

    om_write_str_kv(players, "i", instance_id);
    om_write_str_kv(players, "a", user_id);
    om_write_str_kv(players, "n", player->player_data->username);
    om_write_str_kv(players, "f", player->player_data->figure);
    om_write_str_kv(players, "s", player->player_data->sex);
    sb_add_string(players->sb, "l:");
    sb_add_int_delimeter(players->sb, player->room_user->position->x, ' ');
    sb_add_int_delimeter(players->sb, player->room_user->position->y, ' ');
    sb_add_float_delimeter(players->sb, player->room_user->position->z, (char)13);

    if (strlen(player->player_data->motto) > 0) {
        om_write_str_kv(players, "c", player->player_data->motto);
    }

    if (strcmp(player->room_user->room->room_data->model_data->model_name, "pool_a") == 0
        || strcmp(player->room_user->room->room_data->model_data->model_name, "pool_b") == 0
        || strcmp(player->room_user->room->room_data->model_data->model_name, "md_a") == 0) {

        if (strlen(player->player_data->pool_figure) > 0) {
            om_write_str_kv(players, "p", player->player_data->pool_figure);
        }
    }
}

/**
 * Append user statuses to the packet
 *
 * @param om the outgoing message
 * @param player the player
 */
void append_user_status(outgoing_message *om, session *player) {
    sb_add_int_delimeter(om->sb, player->room_user->instance_id, ' ');
    sb_add_int_delimeter(om->sb, player->room_user->position->x, ',');
    sb_add_int_delimeter(om->sb, player->room_user->position->y, ',');
    sb_add_float_delimeter(om->sb, player->room_user->position->z, ',');
    sb_add_int_delimeter(om->sb, player->room_user->position->head_rotation, ',');
    sb_add_int_delimeter(om->sb, player->room_user->position->body_rotation, '/');

    if (hashtable_size(player->room_user->statuses) > 0) {
        HashTableIter iter;

        TableEntry *entry;
        hashtable_iter_init(&iter, player->room_user->statuses);

        while (hashtable_iter_next(&iter, &entry) != CC_ITER_END) {
            room_user_status *rus = entry->value;

            sb_add_string(om->sb, rus->key);
            sb_add_string(om->sb, rus->value);
            sb_add_string(om->sb, "/");
        }
    }

    sb_add_char(om->sb, 13);
}