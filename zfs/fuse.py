#!/usr/bin/env python3

from __future__ import division, print_function, absolute_import

import cmd, sys
import os
import sys
import errno
import stat
import logging
import errno
import llfuse
from os import fsencode, fsdecode

from zfs.dataset import zfsnode

map_st_mode = {
    's' : stat.S_IFSOCK,
    'l' : stat.S_IFLNK,
    'f' : stat.S_IFREG,
    'b' : stat.S_IFBLK,
    'd' : stat.S_IFDIR,
    'c' : stat.S_IFCHR,
    'p' : stat.S_IFIFO
};

class zfsfuse(llfuse.Operations):
    def __init__(self, dataset):
        super(zfsfuse, self).__init__()
        try:
            self.log = logging.getLogger(__name__)
            self.m = {}
            self.rootdir = dataset.rootdir(llfuse.ROOT_INODE)
            self.rootdir_inode = self.rootdir.inode()
            self._inode_map = { self.rootdir_inode : self.rootdir }
            self.dolog('[+] init for datasets %s' %(str(dataset)))
            self.dolog('[+] rootnodeid: %s' %(llfuse.ROOT_INODE))
        except Exception as e:
            self.dolog("[-] Exception in zfsfuse init %s" %(str(e)))
            raise(e)

    def dolog(self, *argv):
        print(*argv)
        #self.log.debug(*argv)

    def findinode(self, i):
        try:
            val = self._inode_map[i]
        except:
            raise llfuse.FUSEError(errno.ENOENT)
        return val

    def registerinode(self, i, e):
        self._inode_map[i] = e


    ###########################################

    def _getattr(self, e):

        entry = llfuse.EntryAttributes()
        a = e.stattype()
        entry.st_mode = 0o777
        try:
            entry.st_mode |= map_st_mode[a]
        except:
            pass
        #entry.st_mode = ((stat.S_IFDIR if e.isdir() else stat.S_IFREG) | 0o777)
        entry.st_size = e.size()
        entry.st_ino = e.inode()
        entry.st_atime_ns = e.atime() * 1000*1000*1000
        entry.st_ctime_ns = e.ctime() * 1000*1000*1000
        entry.st_mtime_ns = e.mtime() * 1000*1000*1000
        entry.st_gid = os.getgid()
        entry.st_uid = os.getuid()
        return entry

    def getattr(self, inode, ctx=None):

        self.dolog('[>] getattr for %d' %(inode))
        n = self.findinode(inode)
        return self._getattr(n)

    def lookup(self, parent_inode, name, ctx=None):
        name = fsdecode(name)
        self.dolog('[>] lookup for %d [%s]' %(parent_inode, name))
        n = self.findinode(parent_inode)
        d = n.readdir();
        r = {}
        for i in d:
            r[i.name()] = i;
        if name in r:
            e = r[name]
            attr = self._getattr(e)
            self.registerinode(attr.st_ino, e)
            return attr
        print("Cannot find %s" %(name))
        raise llfuse.FUSEError(errno.ENOENT)

    def opendir(self, inode, ctx):
        self.dolog('[>] opendit for %d' %(inode))
        return inode

    def readdir(self, fh, off):
        self.dolog('[>] readdir for %d' %(fh))
        n = self.findinode(fh)
        entries = []
        for e in n.readdir():
            attr = self._getattr(e)
            entries.append((attr.st_ino, e.name(), attr))
        for (ino, name, attr) in sorted(entries):
            if ino <= off:
                continue
            yield (fsencode(name), attr, ino)

    def open(self, inode, flags, ctx):
        n = self.findinode(inode)
        n.extract_file()
        self.dolog('[vvv] open dnode-%d: "%s":%d-bytes inode-%d(tmp:%s)' %(n.dnodeid(), n.abspath(), n.size(), inode, n._cache_file))
        return inode

    def read(self, fh, off, size):
        self.dolog('[+++] read for %d : %d-%d' %(fh, off, size))
        n = self.findinode(fh)
        return n.read(off, size);

    def readlink(self, inode, ctx):
        n = self.findinode(inode)
        b = n.readlink()
        if b is None:
            raise llfuse.FUSEError(errno.ENOENT)
        self.dolog('[+++] readlink for %d -> %s' %(inode,fsencode(b)))
        return fsencode(b)

    def release(self, fd):
        self.dolog('[^^^] release for %d' %(fd))
        n = self.findinode(fd)
        n.release_file()

    def releasedir(self,fd):
        pass

class mountpoint():
    def __init__(self, mountpoint, dataset):
        self.mountpoint = mountpoint
        self.dataset = dataset
        print("[+] mount fuse at " + mountpoint)

    def init_logging(self, debug=False):
        formatter = logging.Formatter('%(asctime)s.%(msecs)03d %(threadName)s: '
                                      '[%(name)s] %(message)s', datefmt="%Y-%m-%d %H:%M:%S")
        handler = logging.StreamHandler()
        handler.setFormatter(formatter)
        root_logger = logging.getLogger()
        if debug:
            handler.setLevel(logging.DEBUG)
            root_logger.setLevel(logging.DEBUG)
        else:
            handler.setLevel(logging.INFO)
            root_logger.setLevel(logging.INFO)
        root_logger.addHandler(handler)

    def mount(self,log=True):
        self.init_logging(log)

        fs = zfsfuse(self.dataset)
        fuse_options = set(llfuse.default_options)
        fuse_options.add('fsname=zfsrescue')
        #fuse_options.add('debug')
        llfuse.init(fs, self.mountpoint, fuse_options)
        try:
            llfuse.main(workers=1)
        except Exception as e:
            print(str(e))
            llfuse.close(unmount=False)
            raise e
        llfuse.close()
