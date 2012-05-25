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

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "chatgui.h"
#include "chet2p.h"

void
cmd_status()
{
	GList *peers, *curpeer;
	peer_info_t *peer_info;
	char buff[BUFFSIZE];

	peers = g_hash_table_get_values(peers_by_id);
	curpeer = peers;

	while (curpeer) {
		peer_info = curpeer->data;

		snprintf(buff, BUFFSIZE, "[%s] is %salive", peer_info->id,
			peer_info->alive ? "" : "not ");
		chat_writeln(FALSE, buff);

		curpeer = curpeer->next;
	}
}

void
_cmd_message(const char *peer_id, const char *message)
{
	int sockfd;
	ssize_t writec;
	char buff[BUFFSIZE];
	peer_info_t *peer_info = NULL;

	peer_info = g_hash_table_lookup(peers_by_id, peer_id);
	if (peer_info == NULL) {
		snprintf(buff, BUFFSIZE, "%s :unknown id", peer_id);
		chat_writeln(TRUE, buff);
		return;
	}

	sockfd = peer_info->sockfd_tcp;
	writec = write(sockfd, message, strlen(message));
	if (writec == strlen(message)) {
		chat_message(MSGDIR_OUT, &peer_id[0], &message[0]);
	}
	else {
		snprintf(buff, BUFFSIZE, "error sending message: %d bytes sent", (int)writec);
		chat_writeln(TRUE, buff);
	}
}

void
cmd_message(const char *line)
{
	int argc;
	char peer_id[BUFFSIZE], message[BUFFSIZE];

	argc = sscanf(line, "%s %[^\n]", peer_id, message);
	if (argc < 2) {
		chat_writeln(TRUE, "Usage: msg <id> <message>");
		return;
	}

	_cmd_message(peer_id, message);
}

void
cmd_exec(const char *line)
{
	int argc;
	char peer_id[BUFFSIZE], command[BUFFSIZE], message[BUFFSIZE];

	argc = sscanf(line, "%s %s", peer_id, command);
	if (argc < 2) {
		chat_writeln(TRUE, "Usage: exec <id> </path/to/command>");
		return;
	}

	snprintf(message, BUFFSIZE, "exec %s\n", command);
	_cmd_message(peer_id, message);
}

void
cmd_leave()
{
	GList *peers, *curpeer;
	peer_info_t *peer_info;
	struct sockaddr_in peeraddr;
	char buffer[BUFFSIZE];

	peers = g_hash_table_get_values(peers_by_id);

	curpeer = peers;
	while (curpeer) {
		peer_info = curpeer->data;
		pthread_cancel(peer_info->poller_tid);
		pthread_join(peer_info->poller_tid, NULL);

		pthread_cancel(peer_info->connect_tid);
		pthread_join(peer_info->connect_tid, NULL);

		peeraddr.sin_family = AF_INET;
		peeraddr.sin_addr.s_addr = peer_info->in_addr;
		peeraddr.sin_port = peer_info->udp_port;

		sendto(peer_info->sockfd_udp, leave, strlen(leave), 0,
			(struct sockaddr *)&peeraddr,
			 sizeof(struct sockaddr_in));
		close(peer_info->sockfd_udp);

		close(peer_info->sockfd_tcp);

		snprintf(buffer, BUFFSIZE, "Leaving %s", peer_info->id);
		chat_writeln(TRUE, buffer);

		curpeer = curpeer->next;
	}

	pthread_cancel(heartbeat_tid);
	pthread_cancel(chatserver_tid);
	should_finish = TRUE;
}
