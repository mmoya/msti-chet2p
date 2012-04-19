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
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <glib.h>
#include <ncurses.h>

#define INPUTLEN 80
#define BUFFSIZE 255

typedef struct {
	char *id;
	in_addr_t in_addr;
	uint16_t udp_port;
	uint16_t tcp_port;
	int sockfd_w;
	int alive;
} peer_info_t;

pthread_mutex_t chatw_mutex;
WINDOW *chat_window, *input_window;
int chat_height, chat_width;

GHashTable *peers_by_id;
GHashTable *peers_by_addr;
char *self_id;
peer_info_t *self_info;

pthread_mutex_t logfile_mutex;
FILE *logfile;

pthread_t tid[2];

void
chat_writeln(int notice, const char *);

void *
heartbeat(void *data)
{
	int sk;
	struct sockaddr_in srvaddr, peeraddr;
	char buffer[BUFFSIZE];
	socklen_t skaddrl;
	size_t read;
	char *pong = "pong\n";
	char line[BUFFSIZE];

	memset(&srvaddr, 0, sizeof(srvaddr));
	memset(&peeraddr, 0, sizeof(peeraddr));

	sk = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	srvaddr.sin_family = AF_INET;
	srvaddr.sin_addr.s_addr = self_info->in_addr;
	srvaddr.sin_port = self_info->udp_port;

	skaddrl = sizeof(peeraddr);

	bind(sk, (struct sockaddr *)&srvaddr, sizeof(srvaddr));

	chat_writeln(TRUE, "Socket binded");

	while ((read = recvfrom(sk, buffer, BUFFSIZE, 0,
			(struct sockaddr *)&peeraddr, &skaddrl)) > 0) {

		if (buffer[read - 1] == '\n')
			buffer[read - 1] = '\0';

		snprintf(line, BUFFSIZE, "Received <%s> from %s:%d",
			buffer, inet_ntoa(peeraddr.sin_addr),
			ntohs(peeraddr.sin_port));
		chat_writeln(TRUE, line);

		if (strncmp(buffer, "ping", BUFFSIZE) == 0) {
			chat_writeln(TRUE, "Sending pong");
			sendto(sk, pong, 5, 0,
				(struct sockaddr *)&peeraddr, skaddrl);
		}
	}

	return NULL;
}

void *
peer_connect(void *data)
{
	int sockfd;
	struct sockaddr_in peeraddr;
	char buffer[BUFFSIZE];
	peer_info_t *peer_info;

	peer_info = data;

	sockfd = socket(PF_INET, SOCK_STREAM, 0);

	peeraddr.sin_family = AF_INET;
	peeraddr.sin_addr.s_addr = peer_info->in_addr;
	peeraddr.sin_port = peer_info->tcp_port;

	if (connect(sockfd, (struct sockaddr *)&peeraddr,
		sizeof(peeraddr)) == 0) {
		snprintf(buffer, BUFFSIZE, "Connected to peer %s:%d",
			inet_ntoa(peeraddr.sin_addr),
			htons(peeraddr.sin_port));
		peer_info->sockfd_w = sockfd;
	}
	else {
		snprintf(buffer, BUFFSIZE, "Error connecting to peer %s:%d",
			inet_ntoa(peeraddr.sin_addr),
			htons(peeraddr.sin_port));
	}

	chat_writeln(TRUE, buffer);
	return NULL;
}

void *
peers_connect(void *data)
{
	GList *peers, *curpeer;
	int peers_len, i;
	pthread_t *tids;

	peers = g_hash_table_get_values(peers_by_id);
	peers_len = g_list_length(peers);
	curpeer = peers;

	tids = (pthread_t *)malloc(sizeof(pthread_t) * peers_len);
	i = 0;

	while (curpeer) {
		if (curpeer->data != self_info)
			pthread_create(&tids[i++], NULL, peer_connect, curpeer->data);
		curpeer = curpeer->next;
	}

	for (i=0; i<peers_len; i++)
		pthread_join(tids[i], NULL);

	free(tids);

	return NULL;
}

void
load_peers(char *filename)
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
	peers_by_addr = g_hash_table_new(g_direct_hash, g_direct_equal);

	peersfile = fopen(filename, "r");
	if (peersfile == NULL) {
		fprintf(stderr, "Error opening %s.\n", filename);
		exit(EXIT_FAILURE);
	}

	while ((read = getline(&buffer, &bufsize, peersfile)) != -1) {
		if (buffer[read - 1] == '\n')
			buffer[read - 1] = '\0';

		for (i=0; i<4; i++)
			tokens[i] = strtok(i == 0 ? buffer : NULL, " ");

		peer_info = (peer_info_t *)malloc(sizeof(peer_info_t));
		id = (char *)malloc(strlen(tokens[0]) + 1);
		strcpy(id, tokens[0]);
		peer_info->id = (char *)malloc(strlen(tokens[0]) + 1);
		strcpy(peer_info->id, tokens[0]);
		in_addr = inet_addr(tokens[1]);
		peer_info->in_addr = in_addr;
		peer_info->udp_port = htons(atoi(tokens[2]));
		peer_info->tcp_port = htons(atoi(tokens[3]));
		peer_info->sockfd_w = -1;
		peer_info->alive = FALSE;

		g_hash_table_insert(peers_by_id, id, peer_info);
		g_hash_table_insert(peers_by_addr,
			GUINT_TO_POINTER(in_addr), peer_info);

		if (buffer) {
			free(buffer);
			buffer = NULL;
		}
	}
}

void
chat_writeln(int notice, const char *line)
{
	pthread_mutex_lock(&chatw_mutex);
	if (notice) {
		waddch(chat_window, '[');
		wattron(chat_window, COLOR_PAIR(1));
		waddstr(chat_window, "LOG");
		wattrset(chat_window, A_NORMAL);
		waddch(chat_window, ']');
		waddch(chat_window, ' ');
	}
	waddstr(chat_window, line);
	waddch(chat_window, '\n');
	waddch(chat_window, ' ');
	wborder(chat_window, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE,
			     ACS_ULCORNER, ACS_URCORNER, ACS_LTEE, ACS_RTEE);
	wrefresh(chat_window);
	wrefresh(input_window);
	pthread_mutex_unlock(&chatw_mutex);
}

int
main(int argc, char *argv[])
{
	int rows, cols;
	char line[INPUTLEN];
	int should_finish = FALSE;

	char *peersfile;
	struct stat st;
	int rc;

	logfile = fopen("/tmp/chet2p.log", "a");
	setbuf(logfile, NULL);
	pthread_mutex_init(&logfile_mutex, NULL);

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
	load_peers(peersfile);
	self_id = argv[2];
	self_info = g_hash_table_lookup(peers_by_id, self_id);
	if (self_info == NULL) {
		fprintf(stderr, "Can't find id %s in %s.\n", self_id, peersfile);
		exit(EXIT_FAILURE);
	}

	initscr();
	start_color();

	init_pair(1, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(2, COLOR_CYAN, COLOR_BLACK);

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

	pthread_create(&tid[0], NULL, heartbeat, NULL);
	pthread_create(&tid[1], NULL, peers_connect, NULL);

	do {
		werase(input_window);
		wborder(input_window, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE,
				      ACS_LTEE, ACS_RTEE, ACS_LLCORNER, ACS_LRCORNER);
		mvwprintw(input_window, 1, 1, "> ");
		wrefresh(input_window);
		wgetnstr(input_window, line, INPUTLEN);

		if (strlen(line) > 0)
			chat_writeln(FALSE, line);
	} while (!should_finish);

	pthread_join(tid[0], NULL);
	pthread_join(tid[1], NULL);

	fclose(logfile);

	endwin();
	exit(EXIT_SUCCESS);
}
