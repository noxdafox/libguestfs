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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "guestfs.h"
#include "full-read.h"
#include "guestfs_protocol.h"
#include "guestfs-internal.h"
#include "guestfs-internal-all.h"
#include "guestfs-internal-actions.h"

enum jblock_flags {
  JBLOCK_UNALLOC = 0x00,
  JBLOCK_ALLOC = 0x01,
  JBLOCK_COMMITTED = 0x02,
};

static char *make_temp_file (guestfs_h *, const char *);
static struct guestfs_extfs_jblock_list *parse_jblock_file (guestfs_h *, FILE *);

/* Takes optional arguments, consult optargs_bitmask. */
struct guestfs_extfs_jblock_list *
guestfs_impl_extfs_journal (guestfs_h *g, const char *mountable,
                            const struct guestfs_extfs_journal_argv *optargs)
{
  int ret = 0;
  int64_t inode = -1;
  CLEANUP_FCLOSE FILE *fp = NULL;
  CLEANUP_UNLINK_FREE char *tmpfile = NULL;

  tmpfile = make_temp_file (g, "extfs_journal");
  if (tmpfile == NULL)
    return NULL;

  if (optargs->bitmask & GUESTFS_EXTFS_JOURNAL_INODE_BITMASK)
    inode = optargs->inode;

  ret = guestfs_internal_extfs_journal (g, mountable, tmpfile, inode);
  if (ret < 0)
    return NULL;

  fp = fopen (tmpfile, "r");
  if (fp == NULL) {
    perrorf (g, "fopen: %s", tmpfile);
    return NULL;
  }

  return parse_jblock_file (g, fp);  /* caller frees */
}

static struct guestfs_extfs_jblock_list *
parse_jblock_file (guestfs_h *g, FILE *fp)
{
  uint32_t index = 0;
  int linepos = 0, count = 0, flags = 0;
  uint64_t journal_block = 0, filesystem_block = 0;
  uint64_t sequence = 0, time_sec = 0, time_nsec = 0;

  struct guestfs_extfs_jblock_list *jblocks = NULL;
  char line[256], allocation[12], type[11], unused[6];

  jblocks = safe_malloc (g, sizeof (*jblocks));
  jblocks->len = 8;
  jblocks->val = safe_malloc (g, jblocks->len * sizeof (*jblocks->val));

  while (fgets (line, 256, fp) != NULL)
    {
      /* Skip lines not containing a journal block. */
      if (!isdigit (line[0]) || line[0] == '0')
        continue;

      /* Resize results array if necessary. */
      if (index == jblocks->len) {
        jblocks->len = 2 * jblocks->len;
        jblocks->val = safe_realloc (g, jblocks->val,
                                     jblocks->len * sizeof (*jblocks->val));
      }

      memset (&jblocks->val[index], 0, sizeof (*jblocks->val));

      /* Read next journal block. */
      count = sscanf (line, "%" SCNu64 ":\t%s %s %s %n",
                      &journal_block, allocation, type, unused, &linepos);
      if (count < 4)
        goto err;

      flags = STREQ (allocation, "Allocated") ? JBLOCK_ALLOC : JBLOCK_UNALLOC;

      if (STREQ (type, "Descriptor")) {
        /* Descriptor blocks mark the beginning of a transaction sequence. */
        count = sscanf (&line[linepos], "(seq: %" SCNu64 ")", &sequence);
        if (count < 1)
          goto err;
      }
      else if (STREQ (type, "FS") && STRNEQLEN (&line[linepos], "Unknown", 7)) {
        /* FS blocks indicate which file system block was changed. */
        count = sscanf (&line[linepos], "%" SCNu64, &filesystem_block);
        if (count < 1)
          goto err;

        jblocks->val[index].flags = flags;
        jblocks->val[index].sequence = sequence;
        jblocks->val[index].journal_block = journal_block;
        jblocks->val[index].filesystem_block = filesystem_block;

        index++;
      }
      else if (STREQ (type, "Commit")) {
        /* Commit blocks confirm the successful writing of listed blocks. */
        count = sscanf (&line[linepos],
                        "(seq: %" SCNu64 ", sec: %" SCNu64 ".%" SCNu64 ")",
                        &sequence, &time_sec, &time_nsec);
        if (count < 3)
          goto err;

        /* Additional information to block changes of the same sequence. */
        for (count = index -1; count >= 0; count--) {
          /* Unallocated sequences might get the descriptor overwritten. */
          if (jblocks->val[count].sequence == 0)
            jblocks->val[count].sequence = sequence;
          else if (jblocks->val[count].sequence != sequence)
            break;

          jblocks->val[count].time_sec = time_sec;
          jblocks->val[count].time_nsec = time_nsec;
          jblocks->val[count].flags |= JBLOCK_COMMITTED;
        }
      }
    }

  jblocks->len = index;
  jblocks->val = safe_realloc (g, jblocks->val,
                               jblocks->len * sizeof (*jblocks->val));

  return jblocks;

    err:
      perrorf (g, "malformed journal block: %" PRIu64, journal_block);
      guestfs_free_extfs_jblock_list (jblocks);

      return NULL;
}

static char *
make_temp_file (guestfs_h *g, const char *name)
{
  int ret = 0;

  ret = guestfs_int_lazy_make_tmpdir (g);
  if (ret < 0)
    return NULL;

  return safe_asprintf (g, "%s/%s%d", g->tmpdir, name, ++g->unique);
}
