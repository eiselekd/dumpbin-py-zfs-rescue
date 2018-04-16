# Copyright (c) 2017 Hristo Iliev <github@hiliev.eu>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


from zfs.objectset import ObjectSet
from zfs.zap import zap_factory, TYPECODES, safe_decode_string
from zfs.blocktree import BlockTree
from zfs.sa import SystemAttr
from zfs.fileobj import FileObj
from zfs.zio import dumppacket

import csv
import time
import tarfile
import os
import tempfile
import datetime

MODE_UR = 0o400
MODE_UW = 0o200
MODE_UX = 0o100
MODE_GR = 0o040
MODE_GW = 0o020
MODE_GX = 0o010
MODE_OR = 0o004
MODE_OW = 0o002
MODE_OX = 0o001

# /* 0 */ "not specified",
# /* 1 */ "FIFO",
# /* 2 */ "Character Device",
# /* 3 */ "3 (invalid)",
# /* 4 */ "Directory",
# /* 5 */ "5 (invalid)",
# /* 6 */ "Block Device",
# /* 7 */ "7 (invalid)",
# /* 8 */ "Regular File",
# /* 9 */ "9 (invalid)",
# /* 10 */ "Symbolic Link",
# /* 11 */ "11 (invalid)",
# /* 12 */ "Socket",
# /* 13 */ "Door",
# /* 14 */ "Event Port",
# /* 15 */ "15 (invalid)",
# TYPECODES = "-pc-d-b-f-l-soe-"

class zfsnode():
    def __init__(self, dataset, dnode, k, mode, v, size, name, inoderoot, inode, abspath):
        self.dataset = dataset
        self.dnode = dnode
        self._stattype = k
        self.mode = mode
        self.datasetid = v
        self._size = size
        self._name = name
        self._directory = None
        self._inoderoot = inoderoot
        self._inode = self._inoderoot + inode
        self._cache_file = None
        self._abspath = abspath
    def name(self):
        return self._name
    def size(self):
        return self._size if not self.isdir() else 0
    def inode(self):
        return self._inode
    def dnodeid(self):
        return self.datasetid
    def isdir(self):
        return self._stattype == "d"
    def isfile(self):
        return self._stattype == "f"
    def abspath(self):
        return self._abspath

    def mtime(self):
        mode = None
        try:
            mode = self.dnode.bonus.zp_mtime[0]
        except:
            pass
        if mode is None:
            mode = datetime.datetime.now().timestamp()
            #print("----------------- mtime:" + datetime.datetime.fromtimestamp(mode).strftime('%Y-%m-%d %H:%M:%S'))
        return mode
    def atime(self):
        mode = None
        try:
            mode = self.dnode.bonus.zp_atime[0]
            #print("----------------- atime:" + datetime.datetime.fromtimestamp(mode).strftime('%Y-%m-%d %H:%M:%S'))
        except:
            pass
        if mode is None:
            mode = datetime.datetime.now().timestamp()
        return mode
    def ctime(self):
        mode = None
        try:
            mode = self.dnode.bonus.zp_ctime[0]
            #print("----------------- ctime:" + datetime.datetime.fromtimestamp(mode).strftime('%Y-%m-%d %H:%M:%S'))
        except:
            pass
        if mode is None:
            mode = datetime.datetime.now().timestamp()
        return mode
    def stattype(self):
        return self._stattype
    def stat(self):
        return { 'st_atime' : 0 ,
                 'st_ctime' : 0,
                 'st_gid' : 1 ,
                 'st_mode' : self.mode,
                 'st_mtime' : 0,
                 'st_nlink' : 0,
                 'st_size' : self.size,
                 'st_uid' : 1 }
    def readdir(self):
        if self._directory is None:
            self._directory = self.dataset.readdir(self.datasetid, self._inoderoot, self._abspath)
        return self._directory
    def readlink(self):
        try:
            return safe_decode_string(self.dnode.bonus.zp_symlink)
        except:
            return None
    def extract_file(self):
        if not self.isfile():
            print("[+] ------------------- type: %s ---------------" %(self._stattype))
            return
        self._cache_file = next(tempfile._get_candidate_names())
        print("[+] temporary file %s" %(self._cache_file))
        self.dataset.extract_file(self.datasetid, self._cache_file)

    def read(self, off, size):
        with open(self._cache_file, 'rb') as f:
            f.seek(off)
            return f.read(size)

    def release_file(self):
        if not self._cache_file is None:
           os.unlink(self._cache_file)
        pass

class Dataset(ObjectSet):

    def __init__(self, vdev, os_dnode, dvas=(0,1)):
        super().__init__(vdev, os_dnode.bonus.bptr, dvas=dvas)
        self._rootdir_id = None

    def analyze_tree(self,start=1):
        self.traverse_dir(self._rootdir_id,depth=3)
        return

        for n in range(start,self.max_obj_id): #range(self.max_obj_id):
            try:
                d = self[n]
            except:
                d=None
                pass
            if d is None:
                # Bad - very likely the blocmax_obj_idk tree is broken
                print("[-]  Object set (partially) unreachable")
                #break
            print("[+]  dnode[{:>2}]={}".format(n, d))

    def analyze_switchsearch(self):
        print("------------- switch analyze -----------")
        a = self[24236154] 
        r = d = self.readdir(24236154, 0, "")
        for i in r:
            print(" > %s" %(i.name()))
        return 
            
            
        for n in range(24236154,25200000):
            e = self[n]
            if (not (e is None)) and e._type == 20:
                print("ddd8> %10d:%s "%(n,str(e)))
                r = d = self.readdir(n, 0, "")
                l = {}
                for i in r:
                    l[i.name()] = i
                    print(" > %s" %(i.name()))
                    if i.name() == "switchbox_gen.py":
                        print (" switchbox_gen.py << parent dir %d" %(n))

    def analyse(self,name=""):
        self.name = name
        if self.broken:
            print("[-]  Dataset is broken")
            return
        # Read the master node
        master_dnode = self[1]
        if master_dnode is None:
            print("[-]  Master node missing/unreachable")
            return
        print("[+]  Master node", master_dnode)
        if master_dnode.type != 21:
            print("[-]  Master node object imax_obj_ids of wrong type")
            return
        z = zap_factory(self._vdev, master_dnode)
        if z:
            self._rootdir_id = z["ROOT"]
            if self._rootdir_id is None:
                z.debug()

            # try load System Attribute Layout and registry:
            try:
                self._sa = SystemAttr(self._vdev, self, z["SA_ATTRS"]);
            except Exception as e:
                print("[-] Unable to parse System Attribute tables: %s" %(str(e)))

        if self._rootdir_id is None:
            print("[-]  Root directory ID is not in master node")
            return
        self.rootdir_dnode = self[self._rootdir_id]
        if self.rootdir_dnode is None:
            print("[-]  Root directory dnode missing/unreachable")
            return
        if self.rootdir_dnode.type != 20:
            print("[-]  Root directory object is of wrong type")
        num_dnodes = min(self.dnodes_per_block, self.max_obj_id+1)
        print("----------------------------------")
        print("[+]  First block of the object set:")
        for n in range(1,32): #range(self.max_obj_id):
            try:
                d = self[n]
            except:
                d=None
                pass
            if d is None:
                # Bad - very likely the block tree is broken
                print("[-]  Object set (partially) unreachable")
                #break
            print("[+]  dnode[{:>2}]={}".format(n, d))

    def prefetch_object_set(self):
        self.prefetch()

    def traverse_dir(self, dir_dnode_id, depth=1, dir_prefix='/'):
        dir_dnode = self[dir_dnode_id]
        if dir_dnode is None:
            print("[-]  Directory dnode {} unreachable".format(dir_dnode_id))
            return
        zap = None
        try:
            zap = zap_factory(self._vdev, dir_dnode)
        except:
            pass
        if zap is None:
            print("[-]  Unable to create ZAP object")
            return
        keys = sorted(zap.keys())
        for name in keys:
            value = zap[name]
            t = value >> 60
            v = value & ~(15 << 60)
            k = TYPECODES[t]
            entry_dnode = self[v]
            if entry_dnode is None:
                mode = "?????????"
                size = "?"
            else:
                try:
                    mode = entry_dnode.bonus.zp_mode
                    size = entry_dnode.bonus.zp_size
                    modes = [
                    'r' if (mode & MODE_UR) else '-',
                    'w' if (mode & MODE_UW) else '-',
                    'x' if (mode & MODE_UX) else '-',
                    'r' if (mode & MODE_GR) else '-',
                    'w' if (mode & MODE_GW) else '-',
                    'x' if (mode & MODE_GX) else '-',
                    'r' if (mode & MODE_OR) else '-',
                    'w' if (mode & MODE_OW) else '-',
                    'x' if (mode & MODE_OX) else '-'
                    ]
                    mode = "".join(modes)
                except:
                    mode = "?????????"
                    size = "?"
            print("{}{} {:>8} {:>14} {}{}".format(k, mode, v, size, dir_prefix, name))
            if k == 'd' and depth > 0:
                self.traverse_dir(v, depth=depth-1, dir_prefix=dir_prefix+name+'/')

    def rootdir(self, inoderoot):
        r = self[self._rootdir_id]
        return zfsnode(self, r, 'd', -1, self._rootdir_id, 0, "/", inoderoot, 0, "")

    def readdir(self, dir_dnode_id, inoderoot, relpath):
        print("r> %d" %(dir_dnode_id))
        dir_dnode = self[dir_dnode_id]
        r = []
        if dir_dnode is None:
            print("[-]  Directory dnode {} unreachable".format(dir_dnode_id))
            return None
        zap = None
        try:
            zap = zap_factory(self._vdev, dir_dnode)
        except:
            pass
        if zap is None:
            print("[-]  Unable to create ZAP object")
            return []
        keys = sorted(zap.keys())
        for name in keys:
            value = zap[name]
            t = value >> 60
            v = value & ~(15 << 60)
            k = TYPECODES[t]
            entry_dnode = self[v]
            if k == 'd' and not entry_dnode._type == 20:
                print("Mismatch")
            print("> %24s: %s : %d : %s" %(name, k, v, str(entry_dnode)))
            mode = 0
            size = 0
            try:
                size = entry_dnode.bonus.zp_size
                mode = entry_dnode.bonus.zp_mode
            except:
                pass
            r.append(zfsnode(self, entry_dnode, k, mode, v, size, name, inoderoot, v, relpath + "/" + name))
        return r

    def export_file_list(self, fname, root_dir_id=None):
        print("[+]  Exporting file list")
        if root_dir_id is None:
            root_dir_id = self._rootdir_id
        with open(fname, 'w', newline='') as csvfile:
            csvwriter = csv.writer(csvfile, dialect="excel-tab")
            self._export_dir(csvwriter, root_dir_id)

    def extract_file(self, file_node_id, target_path):
        print("[+]  Extracting object {} to {}".format(file_node_id, target_path))
        file_dnode = self[file_node_id]
        bt = BlockTree(file_dnode.levels, self._vdev, file_dnode.blkptrs[0])
        num_blocks = file_dnode.maxblkid + 1
        f = open(target_path, "wb")
        total_len = 0
        corrupted = False
        tt = -time.time()
        if file_dnode.bonus.size() > 0:
            for n in range(num_blocks):
                bp = bt[n]
                bad_block = False
                if bp is None:
                    print("[-]  Broken block tree")
                    bad_block = True
                else:
                    block_data,c = self._vdev.read_block(bp, dva=0)
                    if block_data is None:
                        print("[-]  Unreadable block")
                        bad_block = True
                if bad_block:
                    block_data = b'\x00' * file_dnode.datablksize
                    corrupted = True
                f.write(block_data)
                total_len += len(block_data)
                if n % 16 == 0:
                    print("[+]  Block {:>3}/{} total {:>7} bytes".format(n, num_blocks, total_len))
        tt += time.time()
        if tt == 0.0:
            tt = 1.0  # Prevent division by zero for 0-length files
        data_size = min(total_len, file_dnode.bonus.size())
        f.truncate(data_size)
        f.close()
        print("[+]  {} bytes in {:.3f} s ({:.1f} KiB/s)".format(total_len, tt, total_len / (1024 * tt)))
        return not corrupted

    def archive(self, archive_path, dir_node_id=None, skip_objs=None, temp_dir='/tmp'):
        if dir_node_id is None:
            dir_node_id = self._rootdir_id
        if skip_objs is None:
            skip_objs = []
        with tarfile.open(archive_path, 'w:') as tar:
            self._archive(tar, dir_node_id, temp_dir, skip_objs)

    def _archive(self, tar, dir_node_id, temp_dir, skip_objs, dir_prefix=''):
        print("[+]  Archiving directory object {}".format(dir_node_id))
        dir_dnode = self[dir_node_id]
        if dir_dnode is None:
            print("[-]  Archiving failed")
            return
        zap = zap_factory(self._vdev, dir_dnode)
        if zap is None:
            print("[-]  Archiving failed")
            return
        tmp_name = os.path.join(temp_dir, "extract.tmp")
        keys = sorted(zap.keys())
        for name in keys:
            value = zap[name]
            t = value >> 60
            v = value & ~(15 << 60)
            k = TYPECODES[t]
            if v in skip_objs:
                print("[+]  Skipping {} ({}) per request".format(name, v))
                continue
            if k in ['d', 'f', 'l']:
                entry_dnode = self[v]
                if entry_dnode is None:
                    print("[-]  Skipping unreadable object")
                    continue
                file_info = entry_dnode.bonus
                full_name = dir_prefix + name
                print("[+]  Archiving {} ({} bytes)".format(name, file_info.size()))
                if k == 'f':
                    success = self.extract_file(v, tmp_name)
                    if not success:
                        full_name += "._corrupted"
                    tar_info = tar.gettarinfo(name=tmp_name, arcname=full_name)
                    tar_info.uname = ""
                    tar_info.gname = ""
                elif k == 'd':
                    tar_info = tarfile.TarInfo()
                    tar_info.type = tarfile.DIRTYPE
                    tar_info.size = 0
                    tar_info.name = full_name
                else:
                    tar_info = tarfile.TarInfo()
                    tar_info.type = tarfile.SYMTYPE
                    tar_info.size = 0
                    tar_info.name = full_name
                    if file_info.zp_size > len(file_info.zp_inline_content):
                        # Link target is in the file content
                        linkf = FileObj(self._vdev, entry_dnode)
                        link_target = linkf.read(file_info.zp_size)
                        if link_target is None or len(link_target) < file_info.zp_size:
                            print("[-]  Insufficient content for symlink target")
                            # entry_dnode.dump_data('{}/dnode_{}.raw'.format(temp_dir, v))
                            # raise Exception("Insufficient link target content")
                            continue
                        tar_info.linkname = safe_decode_string(link_target)
                    else:
                        # Link target is inline in the bonus data
                        tar_info.linkname = safe_decode_string(file_info.zp_inline_content[:file_info.zp_size])

                #tar_info.mtime = file_info.zp_mtime
                #tar_info.mode = file_info.zp_mode  # & 0x1ff
                #tar_info.uid = file_info.zp_uid
                #tar_info.gid = file_info.zp_gid

                # print("[+]  Archiving {} bytes from {}".format(tar_info.size, tar_info.name))
                # f = FileObj(self._vdev, entry_dnode) if k == 'f' else None
                try:
                    if k == 'f':
                        if os.path.isfile(tmp_name):
                            with open(tmp_name, 'rb') as f:
                                tar.addfile(tar_info, f)
                            os.unlink(tmp_name)
                    else:
                        tar.addfile(tar_info)
                except:
                    print("[-]  Archiving {} failed".format(tar_info.name))
                if k == 'd':
                    self._archive(tar, v, temp_dir, skip_objs, dir_prefix=full_name+'/')

    def _export_dir(self, csv_obj, dir_node_id, dir_prefix='/'):
        print("[+]  Exporting directory object {}".format(dir_node_id))
        dir_dnode = self[dir_node_id]
        if dir_dnode is None:
            csv_obj.writerow([dir_node_id, -1, dir_prefix])
            return
        zap = zap_factory(self._vdev, dir_dnode)
        if zap is None:
            return
        keys = sorted(zap.keys())
        for name in keys:
            value = zap[name]
            t = value >> 60
            v = value & ~(15 << 60)
            k = TYPECODES[t]
            entry_dnode = self[v]
            size = entry_dnode.bonus.zp_size if entry_dnode is not None else -1
            full_name = dir_prefix + name
            print("{} {}".format(v, full_name))
            if k == 'f':
                csv_obj.writerow([v, size, full_name])
            if k == 'l':
                csv_obj.writerow([v, size, full_name + " -> ..."])
            if k == 'd':
                csv_obj.writerow([v, 0, full_name + '/'])
                self._export_dir(csv_obj, v, dir_prefix=full_name + '/')

    @property
    def max_obj_id(self):
        return self._maxdnodeid
