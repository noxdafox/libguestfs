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
#include "full-write.h"

#include "guestfs.h"
#include "guestfs_protocol.h"
#include "guestfs-internal.h"
#include "guestfs-internal-all.h"
#include "guestfs-internal-actions.h"

#ifdef HAVE_LIBTSK

static int deserialise_inode_info
(guestfs_h *g, XDR *xdrs, struct guestfs_tsk_node *node_info);
static size_t read_file(guestfs_h *g, const char *path, char **dest);
static struct guestfs_tsk_node_list *
parse_filesystem_walk0 (guestfs_h *g, char *buf, size_t bufsize);
static void free_nodes(struct guestfs_tsk_node_list *nodes);

struct guestfs_tsk_node_list *
guestfs_impl_filesystem_walk(guestfs_h *g, const char *mountable)
{
  size_t size = 0;

  CLEANUP_FREE char *buf = NULL;
  CLEANUP_UNLINK_FREE char *tmpfile = NULL;

  if (guestfs_int_lazy_make_tmpdir (g) == -1)
    return NULL;

  tmpfile = safe_asprintf(g, "%s/filesystem_walk%d", g->tmpdir, ++g->unique);

  if (guestfs_filesystem_walk0(g, mountable, tmpfile) < 0)
    return NULL;

  if (!(size = read_file(g, tmpfile, &buf)))
    return NULL;

  return parse_filesystem_walk0(g, buf, size);  /* caller frees */
}

/* Read the whole file at path into dest.
 * Return the size of the file, 0 on error.
 */
static size_t read_file(guestfs_h *g, const char *path, char **dest)
{
  int fd = 0;
  size_t size;
  struct stat statbuf;

  if ((fd = open(path, O_RDONLY|O_CLOEXEC)) == -1) {
    perrorf(g, "open: %s", path);
    return 0;
  }

  if (fstat(fd, &statbuf) == -1) {
    perrorf(g, "stat: %s", path);
    close(fd);
    return 0;
  }

  size = statbuf.st_size;
  if (!(*dest = malloc(size))) {
    perrorf(g, "malloc: %zu bytes", size);
    close(fd);
    return 0;
  }

  if (full_read(fd, *dest, size) != size) {
    perrorf(g, "full-read: %s: %zu bytes", path, size);
    close(fd);
    return 0;
  }

  if (close(fd) == -1) {
    perrorf(g, "close: %s", path);
    close(fd);
    return 0;
  }

  return size;
}

/* Parse buf content and populate nodes list.
 * Return a list of tsk_nodes on success, NULL on error.
 */
static struct guestfs_tsk_node_list *
parse_filesystem_walk0 (guestfs_h *g, char *buf, size_t bufsize)
{
  XDR xdr;
  uint32_t index = 0;
  struct guestfs_tsk_node_list *nodes = NULL;

  nodes = safe_malloc(g, sizeof *nodes);
  nodes->len = 0;
  nodes->val = NULL;

  xdrmem_create(&xdr, buf, bufsize, XDR_DECODE);

  for (index = 0; xdr_getpos(&xdr) < bufsize; index++) {
    if (index == nodes->len) {
      nodes->len = 2 * (nodes->len + 1);
      nodes->val = safe_realloc(g, nodes->val,
                                nodes->len * sizeof (struct guestfs_tsk_node));
    }

    if (deserialise_inode_info(g, &xdr, &nodes->val[index]) < 0)
      {
        xdr_destroy(&xdr);
        free_nodes(nodes);

        return NULL;
      }
  }

  xdr_destroy(&xdr);

  nodes->len = index;
  nodes->val = safe_realloc(g, nodes->val,
                            nodes->len * sizeof (struct guestfs_tsk_node));

  return nodes;
}

/* Parse a single XDR encoded node_info.
 * Return 0 on success, -1 on error.
 */
static int
deserialise_inode_info
(guestfs_h *g, XDR *xdrs, struct guestfs_tsk_node *node_info)
{
  size_t len = 0;
  CLEANUP_FREE char *buf = NULL;

  if (!xdr_u_long(xdrs, &len))
    return -1;

  buf = safe_malloc(g, len);

  if (!xdr_string(xdrs, &buf, len))
    return -1;
  if (!xdr_uint64_t(xdrs, &node_info->tsk_inode))
    return -1;
  if (!xdr_uint32_t(xdrs, &node_info->tsk_allocated))
    return -1;

  node_info->tsk_name = safe_strndup(g, buf, len);

  return 0;
}

/* Free the nodes list. */
static void
free_nodes(struct guestfs_tsk_node_list *nodes)
{
  uint32_t index = 0;

  for (index = 0; index < nodes->len; index++)
    if (nodes->val[index].tsk_name != NULL)
      free(nodes->val[index].tsk_name);

  free(nodes);
}

#endif /* !HAVE_LIBTSK */
