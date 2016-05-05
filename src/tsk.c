/* libguestfs
 * Copyright (C) 2016 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <rpc/xdr.h>
#include <rpc/types.h>

#include "full-read.h"

#include "guestfs.h"
#include "guestfs_protocol.h"
#include "guestfs-internal.h"
#include "guestfs-internal-all.h"
#include "guestfs-internal-actions.h"

static struct guestfs_tsk_dirent_list *parse_filesystem_walk (guestfs_h *, char *, size_t);
static int deserialise_dirent_list (guestfs_h *, char *, size_t , struct guestfs_tsk_dirent_list *);

struct guestfs_tsk_dirent_list *
guestfs_impl_filesystem_walk (guestfs_h *g, const char *mountable)
{
  int ret = 0;
  size_t size = 0;
  CLEANUP_FREE char *buf = NULL;
  CLEANUP_UNLINK_FREE char *tmpfile = NULL;

  ret = guestfs_int_lazy_make_tmpdir (g);
  if (ret < 0)
    return NULL;

  tmpfile = safe_asprintf (g, "%s/filesystem_walk%d", g->tmpdir, ++g->unique);

  ret = guestfs_internal_filesystem_walk (g, mountable, tmpfile);
  if (ret < 0)
    return NULL;

  ret = guestfs_int_read_whole_file (g, tmpfile, &buf, &size);
  if (ret < 0)
    return NULL;

  return parse_filesystem_walk (g, buf, size);  /* caller frees */
}

/* Parse buf content and return dirents list.
 * Return a list of tsk_dirent on success, NULL on error.
 */
static struct guestfs_tsk_dirent_list *
parse_filesystem_walk (guestfs_h *g, char *buf, size_t bufsize)
{
  int ret = 0;
  struct guestfs_tsk_dirent_list *dirents = NULL;

  /* Initialise results array. */
  dirents = safe_malloc (g, sizeof (*dirents));
  dirents->len = 8;
  dirents->val = safe_malloc (g, dirents->len * sizeof (*dirents->val));

  /* Deserialise buffer into dirent list. */
  ret = deserialise_dirent_list (g, buf, bufsize, dirents);
  if (ret < 0) {
    guestfs_free_tsk_dirent_list (dirents);
    return NULL;
  }

  return dirents;
}

/* Deserialise buf content and populate the dirent list.
 * Return the number of deserialised dirents, -1 on error.
 */
static int
deserialise_dirent_list (guestfs_h *g, char *buf, size_t bufsize,
                         struct guestfs_tsk_dirent_list *dirents)
{
  XDR xdr;
  bool_t ret = 0;
  uint32_t index = 0;

  xdrmem_create (&xdr, buf, bufsize, XDR_DECODE);

  for (index = 0; xdr_getpos (&xdr) < bufsize; index++) {
    if (index == dirents->len) {
      dirents->len = 2 * dirents->len;
      dirents->val = safe_realloc (g, dirents->val,
                                   dirents->len *
                                   sizeof (*dirents->val));
    }

    memset(&dirents->val[index], 0, sizeof (*dirents->val));
    ret = xdr_guestfs_int_tsk_dirent(&xdr, (guestfs_int_tsk_dirent *)
                                     &dirents->val[index]);
    if (ret == FALSE)
      break;
  }

  xdr_destroy (&xdr);
  dirents->len = index;

  return (ret == TRUE) ? 0 : -1;
}
