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

#include <config.h>

#include "file-edit.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <locale.h>
#include <langinfo.h>
#include <libintl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

#include "guestfs-internal-frontend.h"

static char *generate_random_name (const char *filename);

int
edit_file_editor (guestfs_h *g, const char *filename, const char *editor)
{
  CLEANUP_FREE char *tmpdir = guestfs_get_tmpdir (g);
  CLEANUP_UNLINK_FREE char *tmpfilename = NULL;
  char buf[256];
  CLEANUP_FREE char *newname = NULL;
  CLEANUP_FREE char *cmd = NULL;
  struct stat oldstat, newstat;
  int r, fd;

  /* Download the file and write it to a temporary. */
  if (asprintf (&tmpfilename, "%s/libguestfsXXXXXX", tmpdir) == -1) {
    perror ("asprintf");
    return -1;
  }

  fd = mkstemp (tmpfilename);
  if (fd == -1) {
    perror ("mkstemp");
    return -1;
  }

  snprintf (buf, sizeof buf, "/dev/fd/%d", fd);

  if (guestfs_download (g, filename, buf) == -1) {
    close (fd);
    return -1;
  }

  if (close (fd) == -1) {
    perror (tmpfilename);
    return -1;
  }

  /* Get the old stat. */
  if (stat (tmpfilename, &oldstat) == -1) {
    perror (tmpfilename);
    return -1;
  }

  /* Edit it. */
  if (asprintf (&cmd, "%s %s", editor, tmpfilename) == -1) {
    perror ("asprintf");
    return -1;
  }

  r = system (cmd);
  if (r == -1 || WEXITSTATUS (r) != 0) {
    perror (cmd);
    return -1;
  }

  /* Get the new stat. */
  if (stat (tmpfilename, &newstat) == -1) {
    perror (tmpfilename);
    return -1;
  }

  /* Changed? */
  if (oldstat.st_ctime == newstat.st_ctime &&
      oldstat.st_size == newstat.st_size)
    return 0;

  /* Upload to a new file in the same directory, so if it fails we
   * don't end up with a partially written file.  Give the new file
   * a completely random name so we have only a tiny chance of
   * overwriting some existing file.
   */
  newname = generate_random_name (filename);
  if (!newname)
    return -1;

  /* Write new content. */
  if (guestfs_upload (g, tmpfilename, newname) == -1)
    return -1;

  /* Set the permissions, UID, GID and SELinux context of the new
   * file to match the old file (RHBZ#788641).
   */
  if (guestfs_copy_attributes (g, filename, newname,
      GUESTFS_COPY_ATTRIBUTES_ALL, 1, -1) == -1)
    return -1;

  if (guestfs_mv (g, newname, filename) == -1)
    return -1;

  return 0;
}

static char
random_char (void)
{
  const char c[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  return c[random () % (sizeof c - 1)];
}

static char *
generate_random_name (const char *filename)
{
  char *ret, *p;
  size_t i;

  ret = malloc (strlen (filename) + 16);
  if (!ret) {
    perror ("malloc");
    return NULL;
  }
  strcpy (ret, filename);

  p = strrchr (ret, '/');
  assert (p);
  p++;

  /* Because of "+ 16" above, there should be enough space in the
   * output buffer to write 8 random characters here.
   */
  for (i = 0; i < 8; ++i)
    *p++ = random_char ();
  *p++ = '\0';

  return ret; /* caller will free */
}