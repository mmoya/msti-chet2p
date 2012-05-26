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

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chatgui.h"
#include "chet2p.h"
#include "commands.h"
#include "peers.h"

const static char *ping = "ping\n";

void
exec_command(const char *command)
{
	pid_t pid;
	pid = fork();
	if (pid == 0) {
		execlp(command, command, NULL);
		exit(EXIT_SUCCESS);
	}
}

void *
peer_connect(void *data)
{
	int sockfd;
	struct sockaddr_in peeraddr;
	char buffer[BUFFSIZE], input[BUFFSIZE], *command;
	peer_info_t *peer_info;
	ssize_t nbytes;

	peer_info = data;

	sockfd = socket(PF_INET, SOCK_STREAM, 0);

	peeraddr.sin_family = AF_INET;
	peeraddr.sin_addr.s_addr = peer_info->in_addr;
	peeraddr.sin_port = peer_info->tcp_port;

	if (!connect(sockfd, (struct sockaddr *)&peeraddr,
		sizeof(peeraddr)) == 0) {
		snprintf(buffer, BUFFSIZE, "error connecting to peer %s@%s:%d",
			peer_info->id,
			inet_ntoa(peeraddr.sin_addr),
			htons(peeraddr.sin_port));
		chat_writeln(TRUE, LOG_ERR, buffer);
		return NULL;
	}

	snprintf(buffer, BUFFSIZE, "connected to peer %s@%s:%d, sending id",
		peer_info->id,
		inet_ntoa(peeraddr.sin_addr),
		htons(peeraddr.sin_port));
	chat_writeln(TRUE, LOG_INFO, buffer);

	peer_info->sockfd_tcp = sockfd;

	snprintf(buffer, BUFFSIZE, "id %s\n", self_info->id);
	write(sockfd, buffer, strlen(buffer));

	while ((nbytes = read(sockfd, input, BUFFSIZE)) > 0) {
		input[nbytes] = '\0';

		if (input[nbytes - 1] == '\n')
			input[nbytes - 1] = '\0';

		if (strstr(input, "leave") == input) {
			close(sockfd);
			update_peer_status(peer_info, FALSE);
			break;
		}
		else if (strstr(input, "exec") == input) {
			command = input + 5;
			chat_writeln(TRUE, LOG_INFO, command);
			exec_command(command);
		}
		else {
			chat_message(MSGDIR_IN, peer_info->id, input);
		}
	}

	return NULL;
}

void
update_peer_status(peer_info_t *peer_info, int status) {
	char line[LINESIZE];

	int prev_status = peer_info->alive;
	peer_info->alive = status;

	if (prev_status != status) {
		snprintf(line, LINESIZE, "%s changed status to %s", peer_info->id,
			 status ? "alive" : "not alive");
		chat_writeln(TRUE, LOG_NOTICE, line);
	}

	if (peer_info->alive) {
		if (!peer_info->connect_tid || pthread_kill(peer_info->connect_tid, 0) != 0) {
			pthread_create(&peer_info->connect_tid, NULL, peer_connect, peer_info);
#ifdef DEBUG
			snprintf(line, LINESIZE, "started connect thread %lu for client %s",
				peer_info->connect_tid, peer_info->id);
			chat_writeln(TRUE, LOG_DEBUG, line);
#endif
		}
	}
	else {
		if (peer_info->connect_tid && pthread_kill(peer_info->connect_tid, 0) == 0) {
#ifdef DEBUG
			snprintf(line, LINESIZE, "terminating connect thread %lu for client %s",
				peer_info->connect_tid, peer_info->id);
			chat_writeln(TRUE, LOG_DEBUG, line);
#endif
			pthread_cancel(peer_info->connect_tid);
			close(peer_info->sockfd_tcp);
		}

		if (peer_info->client_tid && pthread_kill(peer_info->client_tid, 0) == 0) {
#ifdef DEBUG
			snprintf(line, LINESIZE, "terminating client thread %lu for client %s",
				peer_info->client_tid, peer_info->id);
			chat_writeln(TRUE, LOG_DEBUG, line);
#endif
			pthread_cancel(peer_info->client_tid);
			close(peer_info->sockfd_tcp_in);
		}
	}
}

void *
peer_poller(void *data)
{
	peer_info_t *peer_info = data;

	struct sockaddr_in peeraddr;
	socklen_t addrlen;
	struct timeval tv;

	char buffer[BUFFSIZE];
	int one, readb, waitsec;
	time_t sent_at, recv_at;

	tv.tv_sec = 1;
	one = 1;

	peer_info->sockfd_udp = socket(PF_INET, SOCK_DGRAM, 0);
	setsockopt(peer_info->sockfd_udp, SOL_SOCKET, SO_RCVTIMEO,
		(struct timeval *)&tv, sizeof(struct timeval));
	setsockopt(peer_info->sockfd_udp, SOL_SOCKET, SO_REUSEADDR,
		&one, sizeof(int));

	peeraddr.sin_family = AF_INET;
	peeraddr.sin_addr.s_addr = peer_info->in_addr;
	peeraddr.sin_port = peer_info->udp_port;

	addrlen = sizeof(struct sockaddr_in);

	snprintf(buffer, BUFFSIZE, "started polling thread for %s@%s:%d",
		peer_info->id,
		inet_ntoa(peeraddr.sin_addr),
		ntohs(peeraddr.sin_port));
	chat_writeln(TRUE, LOG_INFO, buffer);

	while (TRUE) {
		sendto(peer_info->sockfd_udp, ping, strlen(ping), 0,
			(struct sockaddr *)&peeraddr,
			 sizeof(struct sockaddr_in));
		sent_at = time(NULL);
		readb = recvfrom(peer_info->sockfd_udp, buffer, BUFFSIZE,
			0, (struct sockaddr *)&peeraddr, &addrlen);
		recv_at = time(NULL);

		if (readb > 0 && strstr(buffer, "pong") == buffer) {
			update_peer_status(peer_info, TRUE);
		}
		else {
			update_peer_status(peer_info, FALSE);
		}

		waitsec = 5 - (recv_at - sent_at);
		if (waitsec < 0)
		    waitsec = 0;

		sleep(waitsec);
	}

	snprintf(buffer, BUFFSIZE, "finishing polling thread for %s@%s:%d",
		peer_info->id,
		inet_ntoa(peeraddr.sin_addr),
		ntohs(peeraddr.sin_port));
	chat_writeln(TRUE, LOG_INFO, buffer);

	return NULL;
}

void
create_peers_poller()
{
	GList *peers, *curpeer;
	peer_info_t *peer_info;

	peers = g_hash_table_get_values(peers_by_id);

	curpeer = peers;
	while (curpeer) {
		peer_info = curpeer->data;
		pthread_create(&peer_info->poller_tid, NULL, peer_poller, peer_info);
		curpeer = curpeer->next;
	}
}

void
load_peers(char *filename, const char *self_id)
{
	FILE *peersfile;

	char *buffer = NULL;
	size_t bufsize = 0;
	ssize_t read;
	char *tokens[4];
	int i;

	char *id;
	in_addr_t in_addr;
	peer_info_t *peer_info;

	peers_by_id = g_hash_table_new(g_str_hash, g_str_equal);

	peersfile = fopen(filename, "r");
	if (peersfile == NULL) {
		fprintf(stderr, "Error opening %s.\n", filename);
		exit(EXIT_FAILURE);
	}

	while ((read = getline(&buffer, &bufsize, peersfile)) != -1) {
		if (buffer[0] == '#')
			continue;

		if (buffer[read - 1] == '\n')
			buffer[read - 1] = '\0';

		for (i=0; i<4; i++)
			tokens[i] = strtok(i == 0 ? buffer : NULL, " ");

		peer_info = (peer_info_t *)malloc(sizeof(peer_info_t));
		memset(peer_info, 0, sizeof(peer_info_t));
		id = (char *)malloc(strlen(tokens[0]) + 1);
		strcpy(id, tokens[0]);
		peer_info->id = (char *)malloc(strlen(tokens[0]) + 1);
		strcpy(peer_info->id, tokens[0]);
		in_addr = inet_addr(tokens[1]);
		peer_info->in_addr = in_addr;
		peer_info->udp_port = htons(atoi(tokens[2]));
		peer_info->tcp_port = htons(atoi(tokens[3]));
		peer_info->sockfd_tcp = -1;
		peer_info->alive = FALSE;

		if (strcmp(peer_info->id, self_id)) {
			g_hash_table_insert(peers_by_id, id, peer_info);
		}
		else {
			self_info = peer_info;
		}

		if (buffer) {
			free(buffer);
			buffer = NULL;
		}
	}
}
