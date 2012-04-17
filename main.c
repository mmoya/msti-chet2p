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

#include <stdlib.h>
#include <ncurses.h>

int
main(int argc, char *argv[])
{
	int rows, cols;
	WINDOW *chat_window, *input_window;
	int chat_height, chat_width;

	initscr();
	getmaxyx(stdscr, rows, cols);
	refresh();

	chat_height = rows - 2;
	chat_width = cols;

	chat_window = newwin(chat_height, chat_width, 0, 0);
	box(chat_window, 0, 0);
	idlok(chat_window, TRUE);
	scrollok(chat_window, TRUE);
	wsetscrreg(chat_window, 1, chat_height - 1);
	wrefresh(chat_window);

	input_window = newwin(3, cols, rows - 3, 0);
	wborder(input_window, ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE,
			      ACS_LTEE, ACS_RTEE, ACS_LLCORNER, ACS_LRCORNER);
	mvwprintw(input_window, 1, 1, "> ");
	wrefresh(input_window);

	getch();

	endwin();
	exit(EXIT_SUCCESS);
}
