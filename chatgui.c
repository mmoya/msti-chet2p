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

#include "chatgui.h"

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
