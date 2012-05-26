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

#ifndef _CHET2P_H
#define _CHET2P_H

#include <arpa/inet.h>
#include <glib.h>
#include <pthread.h>

#define INPUTLEN 80
#define BUFFSIZE 255
#define LINESIZE 255

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

extern GHashTable *peers_by_id;
extern pthread_t heartbeat_tid;
extern pthread_t chatserver_tid;
extern peer_info_t *self_info;

#endif /* _CHET2P_H */
