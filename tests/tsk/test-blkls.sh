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

# Test the blkls command.

set -e

if [ -n "$SKIP_TEST_BLKLS_SH" ]; then
    echo "$0: test skipped because environment variable is set."
    exit 77
fi

rm -f test-blkls.bin

# Skip if TSK is not supported by the appliance.
if ! guestfish add /dev/null : run : available "sleuthkit"; then
    echo "$0: skipped because TSK is not available in the appliance"
    exit 77
fi

if [ ! -s ../../test-data/phony-guests/blank-fs.img ]; then
    echo "$0: skipped because blank-fs.img is zero-sized"
    exit 77
fi

# download Master File Table ($MFT).
guestfish --ro -a ../../test-data/phony-guests/blank-fs.img <<EOF
run
mount /dev/sda1 /
write /test.txt "$foo$bar$"
rm /test.txt
umount /
blkls /dev/sda1 0 8192 test-blkls.bin
EOF

# test extracted data contains $foo$bar$ string
grep -q "$foo$bar$" test-blkls.bin
if [ $? neq 0 ]; then
    echo "$0: remove data not found."
    exit 1
fi

rm -f test-blkls.bin
