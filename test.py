# Copyright (C) 2015 Scality SA - http://www.scality.com
#
# This file is part of RestBlockDriver.
#
# RestBlockDriver is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# RestBlockDriver is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with RestBlockDriver.  If not, see <http://www.gnu.org/licenses/>.

import contextlib
import functools
import logging
import os.path
import stat
import subprocess
import tempfile
import unittest

SRB_CLASS = '/sys/class/srb'
BLOCK_CLASS = '/sys/class/block'
CDMI_URL = os.environ['SRB_CDMI_URL']

@contextlib.contextmanager
def mkdtemp(*args, **kwargs):
    path = tempfile.mkdtemp(*args, **kwargs)

    try:
        yield path
    finally:
        try:
            os.rmdir(path)
        except:
            logging.exception('Error while removing temporary path %r', path)


@contextlib.contextmanager
def mounted(device, mount_point):
    subprocess.check_call(['mount', device, mount_point])

    try:
        yield
    finally:
        try:
            subprocess.check_call(['umount', mount_point])
        except:
            logging.exception(
                'Failure to unmount %r from %r', device, mount_point)


def flush_caches():
    with open('/proc/sys/vm/drop_caches', 'w') as fd:
        fd.write('3\n')


def srb_loaded():
    with open('/proc/modules', 'r') as fd:
        for line in fd:
            if line.startswith('srb '):
                return True
            else:
                continue

    return False


@contextlib.contextmanager
def srb_class_file(name, mode):
    path = os.path.join(SRB_CLASS, name)
    fd = open(path, mode)

    try:
        yield fd
    finally:
        fd.close()


@contextlib.contextmanager
def srb_device_attribute(device_name, attribute_name, mode):
    path = os.path.join(BLOCK_CLASS, device_name, attribute_name)
    fd = open(path, mode)

    try:
        yield fd
    finally:
        fd.close()


def cm2d(manager, *manager_args, **manager_kwargs):
    '''Turn a context manager into a decorator, sort-of'''

    def wrapper(fun):
        @functools.wraps(fun)
        def wrapped(*args, **kwargs):
            with manager(*manager_args, **manager_kwargs):
                return fun(*args, **kwargs)

        return wrapped

    return wrapper


class setup_srb(object):
    def __init__(self, func=None):
        self._func = func

    def __call__(self, *args, **kwargs):
        if not self._func:
            raise ValueError('No wrapped function')

        with self:
            return self._func(*args, **kwargs)

    def __enter__(self):
        with srb_class_file('add_urls', 'w') as fd:
            fd.write(CDMI_URL)

    def __exit__(self, *exc_info):
        try:
            with srb_class_file('remove_urls', 'w') as fd:
                fd.write(CDMI_URL)
        except:
            logging.exception('Error while removing URL')


@contextlib.contextmanager
def setup_srb_device(volume_name, size, device_name):
    with setup_srb():
        with srb_class_file('create', 'w') as fd:
            fd.write('%s %s' % (volume_name, size))

        with srb_class_file('attach', 'w') as fd:
            fd.write('%s %s' % (volume_name, device_name))

        try:
            yield
        finally:
            try:
                with srb_class_file('detach', 'w') as fd:
                    fd.write(device_name)

                with srb_class_file('destroy', 'w') as fd:
                    fd.write(volume_name)
            except:
                logging.exception('Failure while detaching device')


@unittest.skipUnless(srb_loaded(), 'SRB not loaded')
class TestClass(unittest.TestCase):
    @classmethod
    def srb_urls(cls):
        with srb_class_file('urls', 'r') as fd:
            line = fd.readline()
            return [s.strip() for s in line.split(',')]

    @classmethod
    def srb_volumes(cls):
        with srb_class_file('volumes', 'r') as fd:
            for line in fd:
                yield line.strip()

    def test_class_registered(self):
        self.assertTrue(os.path.isdir('/sys/class/srb'))

    def test_add_invalid_url(self):
        def test(url):
            with srb_class_file('add_urls', 'w') as fd:
                fd.write(url)

        for url in [
                'http://1.2',
                'http://localhost',
                ]:
            self.assertRaisesRegexp(IOError, r'Errno 22', test, url)

    def test_add_and_remove_url(self):
        def test(url):
            with srb_class_file('add_urls', 'w') as fd:
                fd.write(url)

            self.assertIn(url, self.srb_urls())

            with srb_class_file('remove_urls', 'w') as fd:
                fd.write(url)

            self.assertNotIn(url, self.srb_urls())

        test('http://127.0.0.1/')
        test('http://127.0.0.1/cdmi/')
        test('http://127.0.0.1:8000/')
        test('http://127.0.0.1:8000/cdmi/')

    def test_create_and_destroy(self):
        volume_name = 'srb_test_create'

        with setup_srb():
            with srb_class_file('create', 'w') as fd:
                fd.write('%s 1G' % volume_name)

            self.assertIn(volume_name, self.srb_volumes())

            with srb_class_file('destroy', 'w') as fd:
                fd.write(volume_name)

            self.assertNotIn(volume_name, self.srb_volumes())

    def test_create_attach_detach_destroy(self):
        volume_name = 'srb_test_create_attach_detach_destroy'
        device_name = 'srb0'

        with setup_srb():
            with srb_class_file('create', 'w') as fd:
                fd.write('%s 1G' % volume_name)

            self.assertIn(volume_name, self.srb_volumes())

            with srb_class_file('attach', 'w') as fd:
                fd.write('%s %s' % (volume_name, device_name))

            st = os.stat('/dev/%s' % device_name)

            self.assertNotEqual(0, st.st_mode & stat.S_IFBLK)

            with srb_class_file('detach', 'w') as fd:
                fd.write(device_name)

            with srb_class_file('destroy', 'w') as fd:
                fd.write(volume_name)


@unittest.skipUnless(srb_loaded(), 'SRB not loaded')
class TestDevice(unittest.TestCase):
    @cm2d(setup_srb_device, 'srb_test_attribute_srb_name', '1G', 'srb0')
    def test_attribute_srb_name(self):
        with srb_device_attribute('srb0', 'srb_name', 'r') as fd:
            name = fd.read().strip()

            self.assertEqual('srb_test_attribute_srb_name', name)

    @cm2d(setup_srb_device, 'srb_test_attribute_srb_urls', '1G', 'srb0')
    def test_attribute_srb_urls(self):
        with srb_device_attribute('srb0', 'srb_urls', 'r') as fd:
            urls = fd.read().strip()

            expected = '%ssrb_test_attribute_srb_urls' % CDMI_URL
            self.assertEqual(expected, urls)

    @cm2d(setup_srb_device, 'srb_test_attribute_srb_size', '1025M', 'srb0')
    def test_attribute_srb_size(self):
        with srb_device_attribute('srb0', 'srb_size', 'r') as fd:
            size = int(fd.read().strip())

            self.assertEqual(1025 * 1024 * 1024, size)

    @cm2d(setup_srb_device, 'srb_test_fs', '1G', 'srb0')
    def test_fs(self):
        subprocess.check_call(['mkfs.ext4', '/dev/srb0'])

        with mkdtemp() as mnt:
            file1 = os.path.join(mnt, 'file1')
            file2 = os.path.join(mnt, 'file2')

            with mounted('/dev/srb0', mnt):
                with open(file1, 'w') as fd:
                    fd.write('abcdefg')
                    fd.flush()

                with open(file2, 'w') as fd:
                    fd.write('hijklm')
                    fd.flush()

            flush_caches()

            with mounted('/dev/srb0', mnt):
                with open(file1, 'r') as fd:
                    self.assertEqual('abcdefg', fd.read())

                with open(file2, 'r') as fd:
                    self.assertEqual('hijklm', fd.read())
