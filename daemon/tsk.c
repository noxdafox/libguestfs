/* libguestfs - the guestfsd daemon
 * Copyright (C) 2016 Red Hat Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "guestfs_protocol.h"
#include "daemon.h"
#include "actions.h"
#include "optgroups.h"

#ifdef HAVE_LIBTSK

#include <tsk/libtsk.h>
#include <rpc/xdr.h>
#include <rpc/types.h>

static int
open_filesystem(const char *device, TSK_IMG_INFO **img, TSK_FS_INFO **fs);
static TSK_WALK_RET_ENUM
fswalk_callback(TSK_FS_FILE *fsfile, const char *path, void *data);
static char *join_path(const char *path, const char *name);
static int inode_out(guestfs_int_tsk_node *node_info);
static void reply_with_tsk_error(void);

#else

OPTGROUP_LIBTSK_NOT_AVAILABLE

#endif

static int file_out (const char *cmd);
static guestfs_int_tsk_node* parse_ffind (const char *out, int64_t inode);

GUESTFSD_EXT_CMD(str_sleuthkit_probe, icat);

int
optgroup_sleuthkit_available (void)
{
  return prog_exists (str_sleuthkit_probe);
}

int
do_icat (const mountable_t *mountable, int64_t inode)
{
  CLEANUP_FREE char *cmd = NULL;

  /* Inode must be greater than 0 */
  if (inode < 0) {
    reply_with_error ("inode must be >= 0");
    return -1;
  }

  /* Construct the command. */
  if (asprintf (&cmd, "icat -r %s %" PRIi64, mountable->device, inode) == -1) {
    reply_with_perror ("asprintf");
    return -1;
  }

  return file_out (cmd);
}

int
do_blkcat (const mountable_t *mountable, int64_t start, int64_t number)
{
  CLEANUP_FREE char *cmd = NULL;

  /* Data unit address start must be greater than 0 */
  if (start < 0) {
    reply_with_error ("data unit starting address must be >= 0");
    return -1;
  }

  /* Data unit number must be greater than 1 */
  if (number < 1) {
    reply_with_error ("data unit number must be >= 1");
    return -1;
  }

  /* Construct the command. */
  if (asprintf (&cmd, "blkcat %s %" PRIi64 " %" PRIi64,
                mountable->device, start, number) == -1) {
    reply_with_perror ("asprintf");
    return -1;
  }

  return file_out (cmd);
}

int
do_blkls (const mountable_t *mountable, int64_t start, int64_t stop)
{
  CLEANUP_FREE char *cmd = NULL;

  /* Data unit address start must be greater than 0 */
  if (start < 0) {
    reply_with_error ("data unit starting address must be >= 0");
    return -1;
  }

  /* Data unit address end must be greater than start */
  if (stop <= start) {
    reply_with_error ("data unit stopping address must be > starting one");
    return -1;
  }

  /* Construct the command. */
  if (asprintf (&cmd, "blkls %s %" PRIi64 "-%" PRIi64,
                mountable->device, start, stop) == -1) {
    reply_with_perror ("asprintf");
    return -1;
  }

  return file_out (cmd);
}

guestfs_int_tsk_node*
do_find_inode (const mountable_t *mountable, int64_t inode)
{
  int r;
  char buf[32];
  CLEANUP_FREE char *out = NULL, *err = NULL;

  /* Inode must be greater than 0 */
  if (inode < 0) {
    reply_with_error ("inode must be >= 0");
    return NULL;
  }

  snprintf (buf, sizeof buf, "%" PRIi64, inode);

  r = command (&out, &err, "ffind", mountable->device, buf, NULL);
  if (r == -1) {
    reply_with_error ("%s", err);
    return NULL;
  }

  return parse_ffind(out, inode);
}

static guestfs_int_tsk_node*
parse_ffind (const char *out, int64_t inode)
{
  size_t len;
  guestfs_int_tsk_node *ret;

  ret = calloc (1, sizeof *ret);
  if (ret == NULL) {
    reply_with_perror ("calloc");
    return NULL;
  }

  len = strlen(out) - 1;
  ret->tsk_inode = inode;

  if STRPREFIX (out, "File name not found for inode") {
    reply_with_error ("%ld Inode not in use", inode);
    return NULL;
  }
  else if STRPREFIX (out, "* ") {
    ret->tsk_allocated = 0;
    ret->tsk_name = strndup (&out[2], len - 2);
  }
  else if STRPREFIX (out, "//") {
    ret->tsk_allocated = 1;
    ret->tsk_name = strndup (&out[1], len - 1);
  }
  else {
    ret->tsk_allocated = 1;
    ret->tsk_name = strndup (out, len);
  }

  return ret;
}

static int
file_out (const char *cmd)
{
  int r;
  FILE *fp;
  CLEANUP_FREE char *buffer = NULL;

  if (verbose)
    fprintf (stderr, "%s\n", cmd);

  buffer = malloc (GUESTFS_MAX_CHUNK_SIZE);
  if (buffer == NULL) {
    reply_with_perror ("malloc");
    return -1;
  }

  fp = popen (cmd, "r");
  if (fp == NULL) {
    reply_with_perror ("%s", cmd);
    return -1;
  }

  /* Now we must send the reply message, before the file contents.  After
   * this there is no opportunity in the protocol to send any error
   * message back.  Instead we can only cancel the transfer.
   */
  reply (NULL, NULL);

  while ((r = fread (buffer, 1, sizeof buffer, fp)) > 0) {
    if (send_file_write (buffer, r) < 0) {
      pclose (fp);
      return -1;
    }
  }

  if (ferror (fp)) {
    fprintf (stderr, "fread: %m");
    send_file_end (1);		/* Cancel. */
    pclose (fp);
    return -1;
  }

  if (pclose (fp) != 0) {
    fprintf (stderr, "pclose: %m");
    send_file_end (1);		/* Cancel. */
    return -1;
  }

  if (send_file_end (0))	/* Normal end of file. */
    return -1;

  return 0;
}

#ifdef HAVE_LIBTSK


int optgroup_libtsk_available(void)
{
  return 1;
}

int do_filesystem_walk0(const mountable_t *mountable)
{
  int ret = 0;
  TSK_FS_INFO *fs = NULL;
  TSK_IMG_INFO *img = NULL;
  int flags = TSK_FS_DIR_WALK_FLAG_ALLOC | TSK_FS_DIR_WALK_FLAG_UNALLOC |
    TSK_FS_DIR_WALK_FLAG_RECURSE | TSK_FS_DIR_WALK_FLAG_NOORPHAN;

  if (open_filesystem(mountable->device, &img, &fs) < 0)
    return -1;

  reply(NULL, NULL);  /* Reply message. */

  if (tsk_fs_dir_walk(fs, fs->root_inum, flags, fswalk_callback, &ret) != 0) {
    send_file_end(1);	/* Cancel file transfer. */
    reply_with_tsk_error();

    ret = -1;
  }
  else {
    if (send_file_end(0))  /* Normal end of file. */
      ret = -1;

    ret = 0;
  }

  fs->close(fs);
  img->close(img);

  return ret;
}

/* Inspect the device and initialises the img and fs structures.
 * Return 0 on success, -1 on error.
 */
static int
open_filesystem(const char *device, TSK_IMG_INFO **img, TSK_FS_INFO **fs)
{
  const char *images[] = { device };

  if ((*img = tsk_img_open(1, images, TSK_IMG_TYPE_DETECT , 0)) == NULL) {
    reply_with_tsk_error();

    return -1;
  }

  if ((*fs = tsk_fs_open_img(*img, 0, TSK_FS_TYPE_DETECT)) == NULL) {
    reply_with_tsk_error();

    (*img)->close(*img);

    return -1;
  }

  return 0;
}

/* Filesystem walk callback, it gets called on every FS node.
 * Parse the node, encode it into an XDR structure and send it to the appliance.
 * Return 0 on success, -1 on error.
 */
static TSK_WALK_RET_ENUM
fswalk_callback(TSK_FS_FILE *fsfile, const char *path, void *data)
{
  CLEANUP_FREE char *file_name = NULL;
  struct guestfs_int_tsk_node node_info;

  /* Ignore ./ and ../ */
  if (TSK_FS_ISDOT(fsfile->name->name))
    return 0;

  if ((file_name = join_path(path, fsfile->name->name)) == NULL)
    return -1;

  node_info.tsk_name = file_name;
  node_info.tsk_inode = fsfile->name->meta_addr;
  if (fsfile->name->flags & TSK_FS_NAME_FLAG_UNALLOC)
    node_info.tsk_allocated = 0;
  else
    node_info.tsk_allocated = 1;

  return inode_out(&node_info);
}

/* Joins the file name and file path.
 * Return the joined path on success, NULL on failure.
 */
static char *join_path(const char *path, const char *name)
{
  char *buf;
  size_t len;

  path = (path != NULL) ? path : "";
  len = strlen(path) + strlen(name) + 1;

  if (!(buf = malloc(len))) {
    reply_with_perror("malloc");
    return NULL;
  }

  snprintf(buf, len, "%s%s", path, name);

  return buf;
}

/* Serialise node_info into XDR stream and send it to the appliance.
 * Return 0 on success, -1 on error.
 */
static int inode_out(guestfs_int_tsk_node *node_info)
{
  XDR xdr;
  size_t len;
  CLEANUP_FREE char *buf;

  if ((buf = malloc(GUESTFS_MAX_CHUNK_SIZE)) == NULL) {
    reply_with_perror ("malloc");
    return -1;
  }

  /* Serialise tsk_node struct. */
  len = strlen(node_info->tsk_name) + 1;

  xdrmem_create(&xdr, buf, GUESTFS_MAX_CHUNK_SIZE, XDR_ENCODE);
  if (!xdr_u_long(&xdr, &len))
    return -1;
  if (!xdr_string(&xdr, &node_info->tsk_name, len))
    return -1;
  if (!xdr_uint64_t(&xdr, &node_info->tsk_inode))
    return -1;
  if (!xdr_uint32_t(&xdr, &node_info->tsk_allocated))
    return -1;

  /* Resize buffer to actual length. */
  len = xdr_getpos(&xdr);
  xdr_destroy(&xdr);
  if ((buf = realloc(buf, len)) == NULL)
    return -1;

  /* Send serialised tsk_node out. */
  if (send_file_write(buf, len) < 0)
    return -1;

  return 0;
}

/* Parse TSK error and send it to the appliance. */
static void reply_with_tsk_error(void)
{
  const char *buf = NULL;

  if (tsk_error_get_errno() != 0) {
    buf = tsk_error_get();
    reply_with_error("TSK error: %s", buf);
  }
}

#endif /* !HAVE_LIBTSK */
