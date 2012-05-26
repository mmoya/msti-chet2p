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

#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <ncurses.h>

#include "commands.h"
#include "chatgui.h"
#include "chet2p.h"
#include "peers.h"

int chat_height, chat_width;

GHashTable *peers_by_id;
peer_info_t *self_info;

GHashTable *anon_conns;

pthread_t heartbeat_tid;
pthread_t chatserver_tid;
int should_finish = FALSE;

int chatsrvsk;
int heartbtsk;

void
cleanup();

void
sigint_handler(int sig)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	chat_writeln(TRUE, "Handling SIGINT");
	cmd_leave();
	cleanup();

	pthread_exit(EXIT_SUCCESS);
}

void *
heartbeat(void *data)
{
	struct sockaddr_in srvaddr, peeraddr;
	char buffer[BUFFSIZE];
	socklen_t skaddrl;
	size_t read;
	char *pong = "pong\n";
	char line[BUFFSIZE];

	memset(&srvaddr, 0, sizeof(srvaddr));
	memset(&peeraddr, 0, sizeof(peeraddr));

	heartbtsk = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	srvaddr.sin_family = AF_INET;
	srvaddr.sin_addr.s_addr = self_info->in_addr;
	srvaddr.sin_port = self_info->udp_port;

	skaddrl = sizeof(peeraddr);

	bind(heartbtsk, (struct sockaddr *)&srvaddr, sizeof(srvaddr));

	snprintf(line, BUFFSIZE, "listening for udp heartbeats in %s:%d",
		inet_ntoa(srvaddr.sin_addr),
		ntohs(srvaddr.sin_port));
	chat_writeln(TRUE, line);

	while ((read = recvfrom(heartbtsk, buffer, BUFFSIZE, 0,
			(struct sockaddr *)&peeraddr, &skaddrl)) > 0) {

		if (buffer[read - 1] == '\n')
			buffer[read - 1] = '\0';
#ifdef DEBUG
		snprintf(line, BUFFSIZE, "received <%s> from %s:%d",
			buffer, inet_ntoa(peeraddr.sin_addr),
			ntohs(peeraddr.sin_port));
		chat_writeln(TRUE, line);
#endif
		if (strncmp(buffer, "ping", BUFFSIZE) == 0) {
#ifdef DEBUG
			chat_writeln(TRUE, "sending pong");
#endif
			sendto(heartbtsk, pong, 5, 0,
				(struct sockaddr *)&peeraddr, skaddrl);
		}
	}

	return NULL;
}

void *
chatclient(void *data)
{
	int sockfd = *(int *)data;

	peer_info_t *peer_info;
	struct sockaddr_storage peeraddr_stor;
	socklen_t peeraddr_storl = sizeof(peeraddr_stor);
	struct sockaddr_in *peeraddr;
	char peeraddrs[INET_ADDRSTRLEN];
	int port;
	char line[LINESIZE];
	char buffer[BUFFSIZE];
	int nbytes;
	char *command;

	char *id;
	int identified = 0;

	getpeername(sockfd, (struct sockaddr *)&peeraddr_stor, &peeraddr_storl);
	peeraddr = (struct sockaddr_in *)&peeraddr_stor;
	inet_ntop(AF_INET, &peeraddr->sin_addr, peeraddrs, sizeof(peeraddrs));
	port = ntohs(peeraddr->sin_port);

	snprintf(line, LINESIZE, "accepted tcp connection from anon@%s:%d, waiting for id",
		peeraddrs, port);
	chat_writeln(TRUE, line);

	while ((nbytes = read(sockfd, buffer, BUFFSIZE)) > 0) {
		buffer[nbytes] = '\0';

		if (buffer[nbytes - 1] == '\n')
			buffer[nbytes - 1] = '\0';

		if (!identified && strstr(buffer, "id") == buffer) {
			id = buffer + 3;

			peer_info = g_hash_table_lookup(peers_by_id, id);
			if (peer_info) {
				if (peer_info->client_tid && pthread_kill(peer_info->client_tid, 0) == 0) {
					snprintf(line, LINESIZE, "%s is already connected\n", id);
					write(sockfd, line, strlen(line));
					identified = 0;
					continue;
				}

				identified = 1;

				g_hash_table_remove(anon_conns, data);

				peer_info->sockfd_tcp_in = sockfd;
				peer_info->client_tid = pthread_self();
				update_peer_status(peer_info, TRUE);

				snprintf(line, LINESIZE, "tcp connection from %s:%d identified itself as %s",
					peeraddrs, port, peer_info->id);
				chat_writeln(TRUE, line);

				continue;
			}
			else {
				snprintf(line, LINESIZE, "unregistered id %s\n", id);
				write(sockfd, line, strlen(line));
				identified = 0;
			}
		}

		if (!identified) {
			snprintf(line, LINESIZE, "please identify by sending: id <name>\n");
			write(sockfd, line, strlen(line));
			continue;
		}

		if (strstr(buffer, "leave") == buffer) {
			close(sockfd);
			update_peer_status(peer_info, FALSE);
			break;
		}
		else if (strstr(buffer, "exec") == buffer) {
			command = buffer + 5;
			chat_writeln(TRUE, command);
			exec_command(command);
		}
		else {
			chat_message(MSGDIR_IN, peer_info->id, buffer);
		}
	}

	snprintf(line, LINESIZE, "closing tcp connection from %s@%s:%d",
		identified ? peer_info->id : "anon", peeraddrs, port);
	chat_writeln(TRUE, line);

	if (peer_info)
		update_peer_status(peer_info, FALSE);
	g_hash_table_remove(anon_conns, data);
	close(sockfd);

	return NULL;
}

void *
chatserver(void *data)
{
	int connsk, optval;
	struct sockaddr_in srvaddr;
	char line[LINESIZE];
	int *pconnsk;
	pthread_t *client_tid;

	chatsrvsk = socket(PF_INET, SOCK_STREAM, 0);
	setsockopt(chatsrvsk, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	srvaddr.sin_family = AF_INET;
	srvaddr.sin_addr.s_addr = self_info->in_addr;
	srvaddr.sin_port = self_info->tcp_port;

	bind(chatsrvsk, (struct sockaddr *)&srvaddr, sizeof(srvaddr));
	listen(chatsrvsk, 4);

	snprintf(line, LINESIZE, "listening for tcp conns in %s:%d",
		inet_ntoa(srvaddr.sin_addr),
		ntohs(srvaddr.sin_port));
	chat_writeln(TRUE, line);

	anon_conns = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, g_free);

	while (!should_finish) {
		connsk = accept(chatsrvsk, NULL, NULL);

		client_tid = (pthread_t *)g_malloc(sizeof(pthread_t));
		pconnsk = (int *)g_malloc(sizeof(int));
		*pconnsk = connsk;

		g_hash_table_insert(anon_conns, pconnsk, client_tid);
		pthread_create(client_tid, NULL, chatclient, pconnsk);
	}

	return NULL;
}

void
cleanup()
{
	close(heartbtsk);
	close(chatsrvsk);
	pthread_join(heartbeat_tid, NULL);
	pthread_join(chatserver_tid, NULL);
	endwin();
}

int
main(int argc, char *argv[])
{
	int rows, cols;
	char line[INPUTLEN];
	char buff[BUFFSIZE];

	char *peersfile;
	struct stat st;
	int rc;

	sigset_t set;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <peers_file> <self_id>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	peersfile = argv[1];
	rc = stat(peersfile, &st);
	if (rc == -1) {
		if (errno == ENOENT) {
			fprintf(stderr, "Can't open %s.\n", peersfile);
			exit(EXIT_FAILURE);
		}
		else {
			fprintf(stderr, "Error stating %s: %d\n", peersfile, errno);
			exit(EXIT_FAILURE);
		}
	}
	self_info = NULL;
	load_peers(peersfile, argv[2]);
	if (self_info == NULL) {
		fprintf(stderr, "Can't find id %s in %s.\n", argv[2], peersfile);
		exit(EXIT_FAILURE);
	}

	if (isatty(STDIN_FILENO) || isatty(STDOUT_FILENO) || isatty(STDERR_FILENO)) {
		printf("\033c\033(K\033[J\033[0m\033[?25h");
	}

	initscr();
	start_color();

	init_pair(1, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(2, COLOR_CYAN, COLOR_BLACK);
	init_pair(3, COLOR_GREEN, COLOR_BLACK);

	getmaxyx(stdscr, rows, cols);

	chat_height = rows - 2;
	chat_width = cols;

	chat_window = newwin(chat_height, chat_width, 0, 0);
	box(chat_window, 0, 0);
	idlok(chat_window, TRUE);
	scrollok(chat_window, TRUE);
	wsetscrreg(chat_window, 1, chat_height - 1);
	wmove(chat_window, chat_height - 1, 1);
	wrefresh(chat_window);
	pthread_mutex_init(&chatw_mutex, NULL);

	input_window = newwin(3, cols, rows - 3, 0);

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	pthread_create(&heartbeat_tid, NULL, heartbeat, NULL);
	pthread_create(&chatserver_tid, NULL, chatserver, NULL);

	create_peers_poller();

	pthread_sigmask(SIG_UNBLOCK, &set, NULL);
	signal(SIGINT, sigint_handler);

	while (!should_finish) {
		werase(input_window);
		wborder(input_window, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE,
				      ACS_LTEE, ACS_RTEE, ACS_LLCORNER, ACS_LRCORNER);
		mvwprintw(input_window, 1, 1, "> ");
		wrefresh(input_window);
		wgetnstr(input_window, line, INPUTLEN);

		if (strstr(line, "status") == line) {
			chat_writeln(TRUE, "STATUS");
			cmd_status();
		}
		else if (strstr(line, "leave") == line) {
			chat_writeln(TRUE, "LEAVE");
			cmd_leave();
		}
		else if (strstr(line, "msg ") == line) {
			cmd_message(line + 4);
		}
		else if (strstr(line, "bcast ") == line) {
			cmd_broadcast(line + 6);
		}
		else if (strstr(line, "exec ") == line) {
			cmd_exec(line + 5);
		}
		else {
			snprintf(buff, BUFFSIZE, "%s :unknown command", line);
			chat_writeln(TRUE, buff);
		}
	}

	cleanup();

	exit(EXIT_SUCCESS);
}
