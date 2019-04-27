#!/usr/bin/env python3

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


from block_proxy.proxy import BlockProxy
from zfs.label import Label
from zfs.dataset import Dataset
from zfs.objectset import ObjectSet
from zfs.zio import RaidzDevice
from zfs.zio import MirrorDevice
from zfs.zap import zap_factory
from zfs.fuse import mountpoint

import os
from os import path

BLK_PROXY_ADDR = ("localhost", 24892)       # network block server

SWITCHANALYZE=False
#testdisks="raidz1" #"raidz1" #False
#raidtype="raidz1"
testdisks="simple"
raidtype="simple"
if testdisks == "raidz1":
    INITIALDISKS = [ "/dev/loop0" ]
    BLK_INITIAL_DISK = "/dev/loop0"      # device to read the label from
    BLK_PROXY_ADDR = ("files:", "disks.tab")  # local device nodes
    TXG = -1                                    # select specific transaction or -1 for the active one
    DS_TO_ARCHIVE = [68]
elif testdisks == "simple":
    INITIALDISKS = [ "/dev/loop3" ]
    BLK_INITIAL_DISK = "/dev/loop3"      # device to read the label from
    BLK_PROXY_ADDR = ("files:", "diskone.tab")  # local device nodes
    TXG = -1                                    # select specific transaction or -1 for the active one
    DS_TO_ARCHIVE = [68]
else:
    BLK_PROXY_ADDR = ("files:", "datatab.txt")  # local device nodes
    INITIALDISKS = [ "/dev/disk/by-id/ata-WDC_WD30EFRX-68EUZN0_WD-WCC4N1KPRKPX-part1", "/dev/disk/by-id/ata-WDC_WD30EFRX-68EUZN0_WD-WCC4N7ZXC1E0-part1" ]
    BLK_INITIAL_DISK = "/dev/disk/by-id/ata-WDC_WD30EFRX-68EUZN0_WD-WCC4N7ZXC1E0-part1"
    #BLK_INITIAL_DISK = "/dev/disk/by-id/ata-WDC_WD30EFRX-68EUZN0_WD-WCC4N1KPRKPX-part1"      # device to read the label from
    TXG = 108199 #108199        # 108324                           # select specific transaction or -1 for the active one
    DS_TO_ARCHIVE = [42]
    
    #TXG = 108193  #ok
    #TXG = 108325
    #TXG = 108199 #ok
    #TXG = 108331
    #TXG = 108173 #ok
    #TXG = 108337
    #TXG = 108307
    #TXG = 108313
    #TXG = 108219
    #TXG = 108319


DOEXTRACT=False
MOUNTPOINT="/mnt/recover"
TEMP_DIR = "/tmp"
OUTPUT_DIR = "rescued"
DS_OBJECTS = []                             # objects to export
DS_OBJECTS_SKIP = []                        # objects to skip
DS_SKIP_TRAVERSE = []                       # datasets to skip while exporting file lists
FAST_ANALYSIS = True

print("[+] zfs_rescue v0.3183")

lnum = 0
print("[+] Reading label {} on disk {}".format(lnum, BLK_INITIAL_DISK))
bp = BlockProxy(BLK_PROXY_ADDR)
id_l = Label(bp, BLK_INITIAL_DISK)
id_l.read(0)
id_l.debug()
all_disks = id_l.get_vdev_disks()

if raidtype == "raidz1":
    pool_dev = RaidzDevice(all_disks, 1, BLK_PROXY_ADDR, bad=[0], ashift=id_l._ashift, repair=False, dump_dir=OUTPUT_DIR)
else:
    pool_dev = MirrorDevice(all_disks, BLK_PROXY_ADDR, dump_dir=OUTPUT_DIR)

print("[+] Loading uberblocks from child vdevs")
uberblocks = {}
for disk in INITIALDISKS:
    bp = BlockProxy(BLK_PROXY_ADDR)
    l0 = Label(bp, disk)
    l0.read(0)
    l1 = Label(bp, disk)
    l1.read(1)
    ub = l0.find_active_ub()
    ub_found = " (active UB txg {})".format(ub.txg) if ub is not None else ""
    print("[+]  Disk {}: L0 txg {}{}, L1 txg {}".format(disk, l0.get_txg(), ub_found, l1.get_txg()))
    uberblocks[disk] = ub

# print("\n[+] Active uberblocks:")
# for disk in uberblocks.keys():
#     print(disk)
#     uberblocks[disk].debug()

ub = id_l.find_ub_txg(TXG)
if ub:
    root_blkptr = ub.rootbp
    print("[+] Selected uberblock with txg", TXG)
else:
    root_blkptr = uberblocks[BLK_INITIAL_DISK].rootbp
    print("[+] Selected active uberblock from initial disk")

print("[+] Reading MOS: {}".format(root_blkptr))

datasets = {}

# Try all copies of the MOS
for dva in range(3):
    try:
        mos = ObjectSet(pool_dev, root_blkptr, dvas=(dva,))
    except:
        continue
    for n in range(len(mos)):
        d = mos[n]
        print("[+]  dnode[{:>3}]={}".format(n, d))
        if d and d.type == 16:
            datasets[n] = d

            if d.bonus.ds_num_children > 0:
                ds_dir_obj = d.bonus.ds_dir_obj
                dir_obj = mos[ds_dir_obj]
                child_dir_zap = mos[dir_obj.bonus.dd_child_dir_zapobj]
                dir_obj_zap = zap_factory(pool_dev, child_dir_zap)
                print (str(dir_obj_zap))



print("[+] {} root dataset")
rds_z = mos[1]
rds_zap = zap_factory(pool_dev, rds_z)
rds_id = rds_zap['root_dataset']
rdir = mos[rds_id]
cdzap_id = rdir.bonus.dd_child_dir_zapobj
cdzap_z = mos[cdzap_id]
cdzap_zap = zap_factory(pool_dev, cdzap_z)
for k,v in cdzap_zap._entries.items():
    if not k[0:1] == '$':
        child = mos[v]
        cds = child.bonus.dd_head_dataset_obj
        print("child %s with dataset %d" %(k,cds))
        # mos[cds] points to a zap with "bonus  DSL dataset "
        datasets[cds] = mos[cds]

print("[+] {} datasets found".format(len(datasets)))

for dsid in datasets:
    print("[+] Dataset", dsid)
    ds_dnode = datasets[dsid]
    print("[+]  dnode {}".format(ds_dnode))
    print("[+]  creation timestamp {}".format(ds_dnode.bonus.ds_creation_time))
    print("[+]  creation txg {}".format(ds_dnode.bonus.ds_creation_txg))
    print("[+]  {} uncompressed bytes, {} compressed bytes".format(ds_dnode.bonus.ds_uncompressed_bytes, ds_dnode.bonus.ds_compressed_bytes))
    print(" Hirarchical info:")
    print("[ ] ds_num_children: %d" %(ds_dnode.bonus.ds_num_children))


    if FAST_ANALYSIS:
        continue
    ddss = Dataset(pool_dev, ds_dnode)
    ddss.analyse()
    if dsid not in DS_SKIP_TRAVERSE:
        ddss.export_file_list(path.join(OUTPUT_DIR, "ds_{}_filelist.csv".format(dsid)))



if SWITCHANALYZE:
    # search switch:
    for dsid in DS_TO_ARCHIVE:
        ddss = Dataset(pool_dev, datasets[dsid], dvas=(0,1))
        ddss.analyze_switchsearch()
        #exit()

if (not DOEXTRACT) and len(MOUNTPOINT):
    if not (os.path.exists(MOUNTPOINT)):
        try:
            os.makedirs(MOUNTPOINT)
        except:
            pass

    for dsid in DS_TO_ARCHIVE:
        ddss = Dataset(pool_dev, datasets[dsid], dvas=(0,1))
        ddss.analyse(name=("dataset-%d" %(dsid)))
        #ddss.analyze_switchsearch()
        m = mountpoint(MOUNTPOINT, ddss)
        m.mount()
else:

    for dsid in DS_TO_ARCHIVE:
        ddss = Dataset(pool_dev, datasets[dsid], dvas=(0,1))
        ddss.analyse()
        ddss.analyze_tree(start=1)

        # ddss.prefetch_object_set()
        # continue
        if len(DS_OBJECTS) > 0:
            for dnid, objname in DS_OBJECTS:
                ddss.archive(path.join(OUTPUT_DIR, "ds_{}_{}.tar".format(dsid, objname)),
                             dir_node_id=dnid, skip_objs=DS_OBJECTS_SKIP, temp_dir=TEMP_DIR)
        else:
            ddss.archive(path.join(OUTPUT_DIR, "ds_{}.tar".format(dsid)), skip_objs=DS_OBJECTS_SKIP, temp_dir=TEMP_DIR)
