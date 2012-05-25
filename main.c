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
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <ncurses.h>

#define INPUTLEN 80
#define BUFFSIZE 255

typedef struct {
	char *id;
	in_addr_t in_addr;
	uint16_t udp_port;
	uint16_t tcp_port;
	int sockfd_tcp;
	int sockfd_udp;
	int alive;
	pthread_t poller_tid;
	pthread_t connect_tid;
} peer_info_t;

typedef enum {
	MSGDIR_IN,
	MSGDIR_OUT
} msgdir_t;

pthread_mutex_t chatw_mutex;
WINDOW *chat_window, *input_window;
int chat_height, chat_width;

GHashTable *peers_by_id;
peer_info_t *self_info;

pthread_t heartbeat_tid;
int should_finish = FALSE;

const static char *ping = "ping\n";
const static char *leave = "leave\n";

void
chat_writeln(int notice, const char *);

void
chat_message(const msgdir_t, const char *, const char *);

void
cmd_leave();

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
	snprintf(line, BUFFSIZE, "Listening for udp heartbeats in %s:%d",
		inet_ntoa(srvaddr.sin_addr),
		ntohs(srvaddr.sin_port));
	chat_writeln(TRUE, line);

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
		snprintf(buffer, BUFFSIZE, "Error connecting to peer %s@%s:%d",
			peer_info->id,
			inet_ntoa(peeraddr.sin_addr),
			htons(peeraddr.sin_port));
		chat_writeln(TRUE, buffer);
		return NULL;
	}

	snprintf(buffer, BUFFSIZE, "Connected to peer %s@%s:%d",
		peer_info->id,
		inet_ntoa(peeraddr.sin_addr),
		htons(peeraddr.sin_port));
	chat_writeln(TRUE, buffer);

	peer_info->sockfd_tcp = sockfd;

	while ((nbytes = read(sockfd, input, BUFFSIZE)) > 0) {
		input[nbytes] = '\0';

		if (input[nbytes - 1] == '\n')
			input[nbytes - 1] = '\0';

		if (strstr(input, "leave") == input) {
			close(sockfd);
			peer_info->alive = FALSE;
			break;
		}
		else if (strstr(input, "exec") == input) {
			command = input + 5;
			chat_writeln(TRUE, command);
			exec_command(command);
		}
		else {
			chat_message(MSGDIR_IN, peer_info->id, input);
		}
	}

	return NULL;
}

void
create_peers_connect()
{
	GList *peers, *curpeer;
	peer_info_t *peer_info;
	char message[BUFFSIZE];

	peers = g_hash_table_get_values(peers_by_id);

	curpeer = peers;
	while (curpeer) {
		peer_info = curpeer->data;
		snprintf(message, BUFFSIZE, "Creating connect thread for %s", peer_info->id);
		chat_writeln(TRUE, message);
		pthread_create(&peer_info->connect_tid, NULL, peer_connect, peer_info);
		curpeer = curpeer->next;
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
	chat_writeln(TRUE, buffer);

	while (TRUE) {
		sendto(peer_info->sockfd_udp, ping, strlen(ping), 0,
			(struct sockaddr *)&peeraddr,
			 sizeof(struct sockaddr_in));
		sent_at = time(NULL);
		readb = recvfrom(peer_info->sockfd_udp, buffer, BUFFSIZE,
			0, (struct sockaddr *)&peeraddr, &addrlen);
		recv_at = time(NULL);

		if (readb > 0 && strstr(buffer, "pong") == buffer) {
			peer_info->alive = TRUE;
		}
		else {
			peer_info->alive = FALSE;
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
	chat_writeln(TRUE, buffer);

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

void
chat_repaint()
{
	waddch(chat_window, ' ');
	wborder(chat_window, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE,
			     ACS_ULCORNER, ACS_URCORNER, ACS_LTEE, ACS_RTEE);
	wrefresh(chat_window);
	wrefresh(input_window);
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
	chat_repaint();
	pthread_mutex_unlock(&chatw_mutex);
}

void
chat_message(const msgdir_t msgdir, const char *peer_id, const char *message)
{
	pthread_mutex_lock(&chatw_mutex);
	if (msgdir == MSGDIR_OUT) {
		wattron(chat_window, COLOR_PAIR(3));
		waddstr(chat_window, "> ");
	}
	else {
		wattron(chat_window, COLOR_PAIR(2));
	}
	waddstr(chat_window, peer_id);
	wattrset(chat_window, A_NORMAL);
	waddch(chat_window, ' ');
	waddstr(chat_window, message);
	waddch(chat_window, '\n');
	chat_repaint();
	pthread_mutex_unlock(&chatw_mutex);
}

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
	should_finish = TRUE;
}

void
cleanup()
{
	pthread_join(heartbeat_tid, NULL);
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

	create_peers_poller();
	create_peers_connect();

	pthread_sigmask(SIG_UNBLOCK, &set, NULL);
	signal(SIGINT, sigint_handler);

	do {
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
		else if (strstr(line, "msg") == line) {
			cmd_message(line + 3);
		}
		else if (strstr(line, "exec") == line) {
			cmd_exec(line + 4);
		}
		else {
			snprintf(buff, BUFFSIZE, "%s :unknown command", line);
			chat_writeln(TRUE, buff);
		}
	} while (!should_finish);

	cleanup();

	exit(EXIT_SUCCESS);
}
