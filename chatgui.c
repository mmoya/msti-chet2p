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

#include <string.h>
#include <unistd.h>

#include "chatgui.h"

char *prionames[] =
  {
    "EMERG",
    "ALERT",
    "CRIT",
    "ERR",
    "WARN",
    "NOTICE",
    "INFO",
    "DEBUG"
  };

void
init_gui()
{
	int rows, cols;
	int chatp_height, chatp_width;
	int chat_height, chat_width;
	int inputp_height, inputp_width;
	int input_height, input_width;
	char prompt[] = "> ";
	WINDOW *chatp_window, *inputp_window;

	if (isatty(STDIN_FILENO) || isatty(STDOUT_FILENO) || isatty(STDERR_FILENO)) {
		printf("\033c\033(K\033[J\033[0m\033[?25h");
	}

	initscr();
	start_color();
	// cbreak();
	// noecho();

	init_pair(1, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(2, COLOR_CYAN, COLOR_BLACK);
	init_pair(3, COLOR_GREEN, COLOR_BLACK);
	init_pair(4, COLOR_RED, COLOR_BLACK);

	getmaxyx(stdscr, rows, cols);

	inputp_height = 2;
	inputp_width = cols;
	input_height = inputp_height - 1;
	input_width = cols - 2 - strlen(prompt);

	chatp_height = rows - inputp_height;
	chatp_width = cols;
	chat_height = chatp_height - 2;
	chat_width = chatp_width - 2;

	/* chat window */
	chatp_window = newwin(chatp_height, chatp_width, 0, 0);
	wborder(chatp_window, 0, 0, 0, 0, 0, 0, ACS_LTEE, ACS_RTEE);
	wrefresh(chatp_window);
	chat_window = derwin(chatp_window, chat_height, chat_width, 1, 1);
	idlok(chat_window, TRUE);
	scrollok(chat_window, TRUE);
	wsetscrreg(chat_window, 0, chat_height - 1);
	wmove(chat_window, chat_height - 1, 0);
	// touchwin(chatp_window);
	wrefresh(chat_window);

	/* input window */
	inputp_window = newwin(inputp_height, inputp_width, rows - inputp_height, 0);
	mvwaddch(inputp_window, 0,        0, ACS_VLINE);
	mvwaddch(inputp_window, 1,        0, ACS_LLCORNER);
	mvwaddch(inputp_window, 0, cols - 1, ACS_VLINE);
	mvwaddch(inputp_window, 1, cols - 1, ACS_LRCORNER);
	mvwhline(inputp_window, 1, 1, ACS_HLINE, cols - 2);
	mvwprintw(inputp_window, 0, 1, "> ");
	wrefresh(inputp_window);
	input_window = derwin(inputp_window, input_height, input_width,
			      0, strlen(prompt) + 1);

	pthread_mutex_init(&chatw_mutex, NULL);
}

void
chat_repaint()
{
	wrefresh(chat_window);
	wrefresh(input_window);
}

void
chat_writeln(int prefix, int priority, const char *line)
{
	int color_pair;
	pthread_mutex_lock(&chatw_mutex);

	if (priority > LOG_WARNING)
		color_pair = COLOR_PAIR(1);
	else
		color_pair = COLOR_PAIR(4);

	waddch(chat_window, '\n');
	if (prefix) {
		waddch(chat_window, '[');
		wattron(chat_window, color_pair);
		waddstr(chat_window, prionames[priority]);
		wattrset(chat_window, A_NORMAL);
		waddch(chat_window, ']');
		waddch(chat_window, ' ');
	}
	waddstr(chat_window, line);
	chat_repaint();
	pthread_mutex_unlock(&chatw_mutex);
}

void
chat_message(const msgdir_t msgdir, const char *peer_id, const char *message)
{
	pthread_mutex_lock(&chatw_mutex);

	waddch(chat_window, '\n');
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
	chat_repaint();
	pthread_mutex_unlock(&chatw_mutex);
}
