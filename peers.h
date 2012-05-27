/*
 * Copyright © 2012 Maykel Moya <mmoya@mmoya.org>
 *
 * This file is part of chet2p
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _PEERS_H
#define _PEERS_H

#include <glib.h>
#include <netinet/in.h>
#include <pthread.h>

typedef struct {
	char *id;
	in_addr_t in_addr;
	uint16_t udp_port;
	uint16_t tcp_port;
	int sockfd_tcp;
	int sockfd_tcp_in;
	int sockfd_udp;
	int alive;
	pthread_t poller_tid;
	pthread_t connect_tid;
	pthread_t client_tid;
} peer_info_t;

GHashTable *peers_by_id;
peer_info_t *self_info;

void
exec_command(const char *command);

void
update_peer_status(peer_info_t *peer_info, int status);

void *
peer_connect(void *data);

void
create_peers_connect();

void
create_peers_poller();

void
load_peers(char *filename, const char *self_id);

#endif /* _PEERS_H */
