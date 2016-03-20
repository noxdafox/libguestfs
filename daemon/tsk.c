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

static int file_out (const char *cmd);
static guestfs_int_tsknode* parse_ffind (const char *out, int64_t inode);

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

guestfs_int_tsknode*
do_ffind (const mountable_t *mountable, int64_t inode)
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

static guestfs_int_tsknode*
parse_ffind (const char *out, int64_t inode)
{
  size_t len;
  guestfs_int_tsknode *ret;

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
