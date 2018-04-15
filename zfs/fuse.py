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

class specnode():
    def __init__(self, p, inode):
        self._name = p;
        self._inode = inode;
    def name(self):
        return self._name
    def size(self):
        return 0
    def isdir(self):
        return True
    def nodeid(self):
        return self._inode

class zfsfuse(llfuse.Operations):
    def __init__(self, datasets):
        super(zfsfuse, self).__init__()
        try:
            self.log = logging.getLogger(__name__)
            self.m = {}
            self.datasets = datasets
            self.dolog('[+] init for %d datasets %s' %(len(self.datasets), str([i.name for i in self.datasets])))
            self.roots = [ self.register(dataset.rootdir()) for dataset in self.datasets ]
            self.dolog('[+] rootnodeid: %s' %(llfuse.ROOT_INODE))
        except Exception as e:
            self.dolog("[-] Exception in zfsfuse init %s" %(str(e)))
            raise(e)

    def dolog(self, *argv):
        print(*argv)
        #self.log.debug(*argv)
        
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

    def _getattr(self, e):
        stamp = int(1438467123.985654 * 1e9)
        entry = llfuse.EntryAttributes()
        entry.st_mode = ((stat.S_IFDIR if e.isdir() else 0) | 0o755)
        entry.st_size = e.size()
        entry.st_ino = llfuse.ROOT_INODE+e.nodeid()
        entry.st_atime_ns = stamp
        entry.st_ctime_ns = stamp
        entry.st_mtime_ns = stamp
        entry.st_gid = os.getgid()
        entry.st_uid = os.getuid()
        return entry

    def getattr(self, inode, ctx=None):
        
        self.dolog('[>] getattr for %d' %(inode))
        if not inode == llfuse.ROOT_INODE:
            e = self.finddnode(inode);
            return self._getattr(e)
            
        entry = llfuse.EntryAttributes()
        entry.st_ino = inode # inode 
        entry.st_mode = stat.S_IFDIR | 0o777# it's a dir
        entry.st_nlink = 1
        entry.st_uid = os.getuid() # Process UID
        entry.st_gid = os.getgid() # Process GID
        entry.st_rdev = 0
        entry.st_size = 0
        entry.st_blksize = 1
        entry.st_blocks = 1
        entry.generation = 0
        entry.attr_timeout = 1
        entry.entry_timeout = 1
        entry.st_atime_ns = 0 # Access time (ns), 1 Jan 1970
        entry.st_ctime_ns = 0 # Change time (ns)
        entry.st_mtime_ns = 0 # Modification time (ns)
        return entry
    
        entry = llfuse.EntryAttributes()
        entry.st_mode = (stat.S_IFDIR | 0o755)
        entry.st_size = 0
        entry.st_ino = llfuse.ROOT_INODE
        stamp = int(1438467123.985654 * 1e9)
        entry.st_atime_ns = stamp
        entry.st_ctime_ns = stamp
        entry.st_mtime_ns = stamp
        entry.st_gid = os.getgid()
        entry.st_uid = os.getuid()
        return entry

    def lookup(self, parent_inode, name, ctx=None):
        self.dolog('[>] lookup for %d [%s]' %(parent_inode, name))
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
        self.dolog('[>] opendit for %d' %(inode))
        return inode

    def spec(self, e):
        return [specnode(".", e.nodeid()), specnode("..", e.parentnodeid())]
    
    def readdir(self, fh, off):
        self.dolog('[>] readdir for %d' %(fh))
        if fh == llfuse.ROOT_INODE:
            d = [self.finddnode(i) for i in self.roots]
        else:
            e = self.finddnode(fh)
            d = e.readdir()
        for v in d[off:]:
            yield (v.name().encode(encoding='UTF-8'), self._getattr(v), 1 )

    def open(self, inode, flags, ctx):
        raise llfuse.FUSEError(errno.ENOENT)

    def read(self, fh, off, size):
        raise llfuse.FUSEError(errno.ENOENT)

    def release(self, fd):
        pass
    def releasedir(self,fd):
        pass
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

