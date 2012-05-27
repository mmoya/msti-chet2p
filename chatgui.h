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

#ifndef _CHATGUI_H
#define _CHATGUI_H

#include <ncurses.h>
#include <pthread.h>
#include <syslog.h>

typedef enum {
	MSGDIR_IN,
	MSGDIR_OUT
} msgdir_t;

pthread_mutex_t chatw_mutex;
WINDOW *chat_window, *input_window;

void
init_gui();

void
chat_repaint();

void
chat_writeln(int prefix, int priority, const char *line);

void
chat_message(const msgdir_t msgdir, const char *peer_id, const char *message);

#endif /* _CHATGUI_H */
