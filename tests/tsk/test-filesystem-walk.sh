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

# Test the filesystem-walk command.

set -e

if [ -n "$SKIP_TEST_FILESYSTEM_WALK_SH" ]; then
    echo "$0: test skipped because environment variable is set."
    exit 77
fi

# Skip if TSK is not supported by the appliance.
if ! guestfish add /dev/null : run : available "libtsk"; then
    echo "$0: skipped because TSK is not available in the appliance"
    exit 77
fi

if [ ! -s ../../test-data/phony-guests/ubuntu.img ]; then
    echo "$0: skipped because ubuntu.img is zero-sized"
    exit 77
fi

# list content of disk image and store it into a variable
output=$(guestfish --ro -a ../../test-data/phony-guests/ubuntu.img \
                   run : filesystem-walk /dev/sda2 )

# test bin directory is in the list
echo $output | grep -q "{ tsk_name: bin tsk_inode: 32770 tsk_allocated: 1 }"
if [ $? neq 0 ]; then
    echo "$0: /bin not found in files list."
    exit 1
fi
