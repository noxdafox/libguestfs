#!/bin/bash -
# libguestfs
# Copyright (C) 2016 Red Hat Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# Test the extfs_journal command.

set -e

if [ -n "$SKIP_TEST_EXTFS_JOURNAL_SH" ]; then
    echo "$0: test skipped because environment variable is set."
    exit 77
fi

rm -f test-ext3.img

# Skip if TSK is not supported by the appliance.
if ! guestfish add /dev/null : run : available "sleuthkit"; then
    echo "$0: skipped because TSK is not available in the appliance"
    exit 77
fi

# build Ext3 disk image
output=$(
guestfish <<EOF
sparse test-ext3.img 64M
run

# Format the disk.
part-init /dev/sda mbr
part-add /dev/sda p 64     65535
mkfs ext3 /dev/sda1 blocksize:4096

# Mount and touch a file
mount /dev/sda1 /
touch /test
umount /

# List journal content
extfs-journal /dev/sda1
EOF
)

echo $output | grep -zq '{
  journal_block: .*
  sequence: 2
  filesystem_block: 0
  time_sec: .*
  time_nsec: .*
  flags: 3
}'
if [ $? != 0 ]; then
    echo "$0: block 0 not found in journal blocks list."
    echo "Journal content:"
    echo $output
    exit 1
fi

rm -f test-ext3.img
