# libguestfs Python bindings
# Copyright (C) 2012 Red Hat Inc.
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

import os
import shutil
import tempfile
import unittest

import guestfs


class Test830RHBZ1406906(unittest.TestCase):
    def setUp(self):
        self.filename = "rhbz1406906.img"
        self.tempdir = tempfile.mkdtemp()
        self.guestfs = guestfs.GuestFS(python_return_dict=True)

        self.guestfs.disk_create(self.filename, "raw", 512 * 1024 * 1024)
        self.guestfs.add_drive(self.filename)
        self.guestfs.launch()

    def tearDown(self):
        self.guestfs.close()
        os.unlink(self.filename)
        shutil.rmtree(self.tempdir)

    def test_rhbz1406906(self):
        self.guestfs.part_disk("/dev/sda", "mbr")
        self.guestfs.mkfs("ext3", "/dev/sda1", blocksize=1024)
        self.guestfs.mount("/dev/sda1", "/")

        # touch file with illegal unicode character
        open(os.path.join(self.tempdir, "\udcd4"), "w").close()

        self.guestfs.copy_in(self.tempdir, "/")

        # segfault here on Python 3
        try:
            self.guestfs.find("/")
        except UnicodeDecodeError:
            pass
