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
#include "peers.h"

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
		chat_writeln(FALSE, LOG_INFO, buff);

		curpeer = curpeer->next;
	}
}

void
send_message(const peer_info_t *peer_info, const char *message)
{
	ssize_t writec;
	char buff[BUFFSIZE];

	if (!peer_info->alive) {
		snprintf(buff, BUFFSIZE, "%s :not alive", peer_info->id);
		chat_writeln(TRUE, LOG_ERR, buff);
		return;
	}

	writec = write(peer_info->sockfd_tcp, message, strlen(message));
	if (writec == strlen(message)) {
		chat_message(MSGDIR_OUT, &peer_info->id[0], &message[0]);
	}
	else {
		snprintf(buff, BUFFSIZE, "error sending message: %d bytes sent", (int)writec);
		chat_writeln(TRUE, LOG_ERR, buff);
	}
}

void
_cmd_message(const char *peer_id, const char *message)
{
	char line[LINESIZE];
	peer_info_t *peer_info = NULL;

	if (strncmp(self_info->id, peer_id, BUFFSIZE) == 0) {
		chat_writeln(TRUE, LOG_ERR, "That's myself...");
		return;
	}

	peer_info = g_hash_table_lookup(peers_by_id, peer_id);
	if (peer_info == NULL) {
		snprintf(line, LINESIZE, "%s :unknown id", peer_id);
		chat_writeln(TRUE, LOG_ERR, line);
		return;
	}

	send_message(peer_info, message);
}

void
cmd_message(const char *line)
{
	GList *peers, *curpeer;
	peer_info_t *peer_info;
	int argc;
	char peer_id[BUFFSIZE], message[BUFFSIZE];

	argc = sscanf(line, "%s %[^\n]", peer_id, message);
	if (argc < 2) {
		chat_writeln(TRUE, LOG_ERR, "Usage: msg <id | -b> <message>");
		return;
	}

	if (strstr(&peer_id[0], "-b") == &peer_id[0]) {
		chat_writeln(TRUE, LOG_INFO, "Broadcasting");

		peers = g_hash_table_get_values(peers_by_id);
		curpeer = peers;

		while (curpeer) {
			peer_info = curpeer->data;
			if (peer_info->alive)
				send_message(peer_info, message);
			curpeer = curpeer->next;
		}
	}
	else
		_cmd_message(peer_id, message);
}

void
cmd_exec(const char *line)
{
	int argc;
	char peer_id[BUFFSIZE], command[BUFFSIZE], message[BUFFSIZE];

	argc = sscanf(line, "%s %s[^\n]", peer_id, command);
	if (argc < 2) {
		chat_writeln(TRUE, LOG_ERR, "Usage: exec <id> </path/to/command>");
		return;
	}

	snprintf(message, BUFFSIZE, "exec %s", command);
	_cmd_message(peer_id, message);
}

void
cmd_broadcast(const char *line) {
	GList *peers, *curpeer;
	peer_info_t *peer_info;
	int argc;
	char message[BUFFSIZE];

	argc = sscanf(line, "%s[^\n]", message);
	if (argc < 1) {
		chat_writeln(TRUE, LOG_ERR, "Usage: bcast <message>");
		return;
	}

	peers = g_hash_table_get_values(peers_by_id);
	curpeer = peers;

	while (curpeer) {
		peer_info = curpeer->data;
		if (peer_info->alive)
			send_message(peer_info, message);
		curpeer = curpeer->next;
	}
}
