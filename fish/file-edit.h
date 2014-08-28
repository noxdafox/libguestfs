/* libguestfs - shared file editing
 * Copyright (C) 2009-2014 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef FISH_FILE_EDIT_H
#define FISH_FILE_EDIT_H

#include <guestfs.h>

/**
 * Edit 'filename' using the specified 'editor' application.
 *
 * Returns -1 for failure, 0 otherwise.
 */
extern int edit_file_editor (guestfs_h *g, const char *filename,
                             const char *editor);

#endif