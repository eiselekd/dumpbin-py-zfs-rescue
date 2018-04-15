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

from zfs.dataset import zfsnode

class wrap():
    def __init__(self, f):
        self.fh = f

class zfsfuse(llfuse.Operations):
    def __init__(self, datasets):
        super(zfsfuse, self).__init__()
        try:
            self.log = logging.getLogger(__name__)
            self.m = {}
            self.datasets = datasets
            self.dolog('[+] init for %d datasets %s' %(len(self.datasets), str([i.name for i in self.datasets])))
            self.roots = [ self.register(dataset.rootdir()) for dataset in self.datasets ]
        except Exception as e:
            self.dolog("[-] Exception in zfsfuse init %s" %(str(e)))
            raise(e)

    def dolog(self, *argv):
        print(*argv)
        self.log.debug(*argv)
        
    def finddnode(self, i):
        if i in self.m:
            return self.m[i]
        return None
    
    def register(self, e):
        i = llfuse.ROOT_INODE+e.nodeid()
        self.m[i] = e;
        return i 

    def _path_join(self, prefix, partial):
        if partial.startswith("/"):
            partial = partial[1:]
        path = os.path.join(prefix, partial)
        return path

    ###########################################
    
    def getattr(self, inode, ctx=None):
        
        self.dolog('getattr for %d' %(inode))
        entry = llfuse.EntryAttributes()
        if inode == llfuse.ROOT_INODE:
            entry.st_mode = (stat.S_IFDIR | 0o755)
            entry.st_size = 0
            entry.st_ino = llfuse.ROOT_INODE
        else:
            e = self.finddnode(parent_inode);
            entry.st_mode = ((stat.S_IFDIR if e.isdir() else 0) | 0o755)
            entry.st_size = e.size()
            entry.st_ino = llfuse.ROOT_INODE+e.nodeid()
            
        stamp = int(1438467123.985654 * 1e9)
        entry.st_atime_ns = stamp
        entry.st_ctime_ns = stamp
        entry.st_mtime_ns = stamp
        entry.st_gid = os.getgid()
        entry.st_uid = os.getuid()
        return entry

    def lookup(self, parent_inode, name, ctx=None):
        self.dolog('lookup for %d[%s]' %(parent_inode, name))
        if parent_inode == llfuse.ROOT_INODE:
            d = [self.finddnode(i) for i in self.roots]
        else:
            d = self.finddnode(parent_inode).readdir();
        r = {}
        for i in d:
            r[i.name()] = i;
        if name in r:
            return self.register(r[name])
        raise llfuse.FUSEError(errno.ENOENT)

    def opendir(self, inode, ctx):
        self.dolog('opendit for %d' %(inode))
        e = self.finddnode(inode);
        if e is None or not e.stattype == 4:
            raise llfuse.FUSEError(errno.ENOENT)
        return inode

    def readdir(self, fh, off):
        self.dolog('readdir for %d' %(fh))
        if fh == llfuse.ROOT_INODE:
            d = [self.finddnode(i) for i in self.roots]
        else:
            d = self.finddnode(fh).readdir()
        for v in d[off:]:
            yield (v.name(), self.register(v), 1)

    def open(self, inode, flags, ctx):
        raise llfuse.FUSEError(errno.ENOENT)

    def read(self, fh, off, size):
        raise llfuse.FUSEError(errno.ENOENT)

class mountpoint():
    def __init__(self, mountpoint, datasets):
        self.mountpoint = mountpoint
        self.datasets = datasets
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

        fs = zfsfuse(self.datasets)
        fuse_options = set(llfuse.default_options)
        fuse_options.add('fsname=zfsrescue')
        fuse_options.add('debug')
        llfuse.init(fs, self.mountpoint, fuse_options)
        try:
            llfuse.main(workers=1)
        except Exception as e:
            print(str(e))
            llfuse.close(unmount=False)
            raise
        llfuse.close()

