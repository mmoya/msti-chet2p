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

#include <arpa/inet.h>
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

pthread_t heartbeat_tid;
pthread_t chatserver_tid;
pthread_t main_tid;

int chatsrvsk;
int heartbtsk;

const static char *leave = "leave\n";

void
cleanup();

void
sigint_handler(int sig)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	chat_writeln(TRUE, LOG_INFO, "Handling SIGINT");
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
	int retval;

	memset(&srvaddr, 0, sizeof(srvaddr));
	memset(&peeraddr, 0, sizeof(peeraddr));

	heartbtsk = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	srvaddr.sin_family = AF_INET;
	srvaddr.sin_addr.s_addr = self_info->in_addr;
	srvaddr.sin_port = self_info->udp_port;

	skaddrl = sizeof(peeraddr);

	retval = bind(heartbtsk, (struct sockaddr *)&srvaddr, sizeof(srvaddr));
	if (retval != 0) {
		snprintf(line, BUFFSIZE, "error binding to udp port %d, exiting",
			ntohs(srvaddr.sin_port));
		chat_writeln(TRUE, LOG_CRIT, line);
		sleep(3);
		pthread_kill(main_tid, SIGINT);
		return NULL;
	}

	snprintf(line, BUFFSIZE, "listening for udp heartbeats in %s:%d",
		inet_ntoa(srvaddr.sin_addr),
		ntohs(srvaddr.sin_port));
	chat_writeln(TRUE, LOG_INFO, line);

	while ((read = recvfrom(heartbtsk, buffer, BUFFSIZE, 0,
			(struct sockaddr *)&peeraddr, &skaddrl)) > 0) {

		if (buffer[read - 1] == '\n')
			buffer[read - 1] = '\0';
#ifdef DEBUG
		snprintf(line, BUFFSIZE, "received <%s> from %s:%d",
			buffer, inet_ntoa(peeraddr.sin_addr),
			ntohs(peeraddr.sin_port));
		chat_writeln(TRUE, LOG_INFO, line);
#endif
		if (strncmp(buffer, "ping", BUFFSIZE) == 0) {
#ifdef DEBUG
			chat_writeln(TRUE, LOG_INFO, "sending pong");
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
	free(data);

	peer_info_t *peer_info;
	char line[LINESIZE];
	char buffer[BUFFSIZE];
	int nbytes;
	char *command;

	char *id;
	int identified = 0;

#ifdef DEBUG
	struct sockaddr_storage peeraddr_stor;
	socklen_t peeraddr_storl = sizeof(peeraddr_stor);
	struct sockaddr_in *peeraddr;
	char peeraddrs[INET_ADDRSTRLEN];
	int port;

	getpeername(sockfd, (struct sockaddr *)&peeraddr_stor, &peeraddr_storl);
	peeraddr = (struct sockaddr_in *)&peeraddr_stor;
	inet_ntop(AF_INET, &peeraddr->sin_addr, peeraddrs, sizeof(peeraddrs));
	port = ntohs(peeraddr->sin_port);

	snprintf(line, LINESIZE, "accepted tcp connection from anon@%s:%d, waiting for id",
		peeraddrs, port);
	chat_writeln(TRUE, LOG_DEBUG, line);
#endif
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
					return NULL;
				}

				identified = 1;

				peer_info->sockfd_tcp_in = sockfd;
				peer_info->client_tid = pthread_self();
				update_peer_status(peer_info, TRUE);
#ifdef DEBUG
				snprintf(line, LINESIZE, "tcp connection from %s:%d identified itself as %s",
					peeraddrs, port, peer_info->id);
				chat_writeln(TRUE, LOG_DEBUG, line);
#endif
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
			shutdown(sockfd, 2);
			break;
		}
		else if (strstr(buffer, "exec") == buffer) {
			command = buffer + 5;
			snprintf(line, LINESIZE, "exec %s", command);
			chat_writeln(TRUE, LOG_NOTICE, line);
			exec_command(command);
		}
		else {
			chat_message(MSGDIR_IN, peer_info->id, buffer);
		}
	}
#ifdef DEBUG
	snprintf(line, LINESIZE, "closing tcp connection from %s@%s:%d",
		identified ? peer_info->id : "anon", peeraddrs, port);
	chat_writeln(TRUE, LOG_DEBUG, line);
#endif
	if (peer_info)
		update_peer_status(peer_info, FALSE);

	return NULL;
}

void *
chatserver(void *data)
{
	int connsk, optval;
	struct sockaddr_in srvaddr;
	char line[LINESIZE];
	int *pconnsk;
	pthread_t client_tid;
	int retval;

	chatsrvsk = socket(PF_INET, SOCK_STREAM, 0);
	setsockopt(chatsrvsk, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	srvaddr.sin_family = AF_INET;
	srvaddr.sin_addr.s_addr = self_info->in_addr;
	srvaddr.sin_port = self_info->tcp_port;

	retval = bind(chatsrvsk, (struct sockaddr *)&srvaddr, sizeof(srvaddr));
	if (retval != 0) {
		snprintf(line, BUFFSIZE, "error binding to tcp port %d, exiting",
			ntohs(srvaddr.sin_port));
		chat_writeln(TRUE, LOG_CRIT, line);
		sleep(3);
		pthread_kill(main_tid, SIGINT);
		return NULL;
	}

	listen(chatsrvsk, 4);

	snprintf(line, LINESIZE, "listening for tcp conns in %s:%d",
		inet_ntoa(srvaddr.sin_addr),
		ntohs(srvaddr.sin_port));
	chat_writeln(TRUE, LOG_INFO, line);

	while (TRUE) {
		connsk = accept(chatsrvsk, NULL, NULL);

		pconnsk = (int *)malloc(sizeof(int));
		*pconnsk = connsk;

		pthread_create(&client_tid, NULL, chatclient, pconnsk);
	}

	return NULL;
}

void
cleanup()
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

		pthread_cancel(peer_info->client_tid);
		pthread_join(peer_info->client_tid, NULL);

		peeraddr.sin_family = AF_INET;
		peeraddr.sin_addr.s_addr = peer_info->in_addr;
		peeraddr.sin_port = peer_info->udp_port;

		sendto(peer_info->sockfd_udp, leave, strlen(leave), 0,
			(struct sockaddr *)&peeraddr,
			 sizeof(struct sockaddr_in));
		close(peer_info->sockfd_udp);

		close(peer_info->sockfd_tcp);
		close(peer_info->sockfd_tcp_in);

		snprintf(buffer, BUFFSIZE, "Leaving %s", peer_info->id);
		chat_writeln(TRUE, LOG_INFO, buffer);

		curpeer = curpeer->next;
	}

	pthread_cancel(heartbeat_tid);
	pthread_cancel(chatserver_tid);

	close(heartbtsk);
	close(chatsrvsk);

	pthread_join(heartbeat_tid, NULL);
	pthread_join(chatserver_tid, NULL);
	endwin();
}

int
main(int argc, char *argv[])
{
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

	init_gui();

	main_tid = pthread_self();

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	pthread_create(&heartbeat_tid, NULL, heartbeat, NULL);
	pthread_create(&chatserver_tid, NULL, chatserver, NULL);

	create_peers_poller();

	pthread_sigmask(SIG_UNBLOCK, &set, NULL);
	signal(SIGINT, sigint_handler);

	usleep(250000);
	chat_writeln(TRUE, LOG_INFO, "Client ready...");

	while(TRUE) {
		werase(input_window);
		wgetnstr(input_window, line, INPUTLEN);

		if (strstr(line, "status") == line) {
			chat_writeln(TRUE, LOG_INFO, "STATUS");
			cmd_status();
		}
		else if (strstr(line, "leave") == line) {
			werase(input_window);
			chat_writeln(TRUE, LOG_INFO, "Leaving...");
			sleep(1);
			break;
		}
		else if (strstr(line, "msg") == line) {
			cmd_message(line + 3);
		}
		else if (strstr(line, "bcast") == line) {
			cmd_broadcast(line + 5);
		}
		else if (strstr(line, "exec") == line) {
			cmd_exec(line + 4);
		}
		else {
			snprintf(buff, BUFFSIZE, "%s :unknown command", line);
			chat_writeln(TRUE, LOG_ERR, buff);
		}
	}

	sigint_handler(SIGINT);

	exit(EXIT_SUCCESS);
}
