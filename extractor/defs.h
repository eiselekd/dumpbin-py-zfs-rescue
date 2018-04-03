#ifndef _DEFS_H_
#define _DEFS_H_

#include <stdint.h>

/*
 * Each block has a 256-bit checksum -- strong enough for cryptographic hashes.
 */
typedef struct zio_cksum {
	uint64_t	zc_word[4];
} zio_cksum_t;

/*
 * All SPA data is represented by 128-bit data virtual addresses (DVAs).
 * The members of the dva_t should be considered opaque outside the SPA.
 */
typedef struct dva {
	uint64_t	dva_word[2];
} dva_t;

#define	SPA_BLKPTRSHIFT	7		/* blkptr_t is 128 bytes	*/
#define	SPA_DVAS_PER_BP	3		/* Number of DVAs in a bp	*/

/*
 * Each block is described by its DVAs, time of birth, checksum, etc.
 * The word-by-word, bit-by-bit layout of the blkptr is as follows:
 *
 *	64	56	48	40	32	24	16	8	0
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 0	|		vdev1		| GRID  |	  ASIZE		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 1	|G|			 offset1				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 2	|		vdev2		| GRID  |	  ASIZE		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 3	|G|			 offset2				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 4	|		vdev3		| GRID  |	  ASIZE		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 5	|G|			 offset3				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 6	|BDX|lvl| type	| cksum |E| comp|    PSIZE	|     LSIZE	|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 7	|			padding					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 8	|			padding					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 9	|			physical birth txg			|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * a	|			logical birth txg			|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * b	|			fill count				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * c	|			checksum[0]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * d	|			checksum[1]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * e	|			checksum[2]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * f	|			checksum[3]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * Legend:
 *
 * vdev		virtual device ID
 * offset	offset into virtual device
 * LSIZE	logical size
 * PSIZE	physical size (after compression)
 * ASIZE	allocated size (including RAID-Z parity and gang block headers)
 * GRID		RAID-Z layout information (reserved for future use)
 * cksum	checksum function
 * comp		compression function
 * G		gang block indicator
 * B		byteorder (endianness)
 * D		dedup
 * X		encryption
 * E		blkptr_t contains embedded data (see below)
 * lvl		level of indirection
 * type		DMU object type
 * phys birth	txg of block allocation; zero if same as logical birth txg
 * log. birth	transaction group in which the block was logically born
 * fill count	number of non-zero blocks under this bp
 * checksum[4]	256-bit checksum of the data this bp describes
 */

/*
 * The blkptr_t's of encrypted blocks also need to store the encryption
 * parameters so that the block can be decrypted. This layout is as follows:
 *
 *	64	56	48	40	32	24	16	8	0
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 0	|		vdev1		| GRID  |	  ASIZE		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 1	|G|			 offset1				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 2	|		vdev2		| GRID  |	  ASIZE		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 3	|G|			 offset2				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 4	|			salt					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 5	|			IV1					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 6	|BDX|lvl| type	| cksum |E| comp|    PSIZE	|     LSIZE	|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 7	|			padding					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 8	|			padding					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 9	|			physical birth txg			|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * a	|			logical birth txg			|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * b	|		IV2		|	    fill count		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * c	|			checksum[0]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * d	|			checksum[1]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * e	|			MAC[0]					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * f	|			MAC[1]					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * Legend:
 *
 * salt		Salt for generating encryption keys
 * IV1		First 64 bits of encryption IV
 * X		Block requires encryption handling (set to 1)
 * E		blkptr_t contains embedded data (set to 0, see below)
 * fill count	number of non-zero blocks under this bp (truncated to 32 bits)
 * IV2		Last 32 bits of encryption IV
 * checksum[2]	128-bit checksum of the data this bp describes
 * MAC[2]	128-bit message authentication code for this data
 *
 * The X bit being set indicates that this block is one of 3 types. If this is
 * a level 0 block with an encrypted object type, the block is encrypted
 * (see BP_IS_ENCRYPTED()). If this is a level 0 block with an unencrypted
 * object type, this block is authenticated with an HMAC (see
 * BP_IS_AUTHENTICATED()). Otherwise (if level > 0), this bp will use the MAC
 * words to store a checksum-of-MACs from the level below (see
 * BP_HAS_INDIRECT_MAC_CKSUM()). For convenience in the code, BP_IS_PROTECTED()
 * refers to both encrypted and authenticated blocks and BP_USES_CRYPT()
 * refers to any of these 3 kinds of blocks.
 *
 * The additional encryption parameters are the salt, IV, and MAC which are
 * explained in greater detail in the block comment at the top of zio_crypt.c.
 * The MAC occupies half of the checksum space since it serves a very similar
 * purpose: to prevent data corruption on disk. The only functional difference
 * is that the checksum is used to detect on-disk corruption whether or not the
 * encryption key is loaded and the MAC provides additional protection against
 * malicious disk tampering. We use the 3rd DVA to store the salt and first
 * 64 bits of the IV. As a result encrypted blocks can only have 2 copies
 * maximum instead of the normal 3. The last 32 bits of the IV are stored in
 * the upper bits of what is usually the fill count. Note that only blocks at
 * level 0 or -2 are ever encrypted, which allows us to guarantee that these
 * 32 bits are not trampled over by other code (see zio_crypt.c for details).
 * The salt and IV are not used for authenticated bps or bps with an indirect
 * MAC checksum, so these blocks can utilize all 3 DVAs and the full 64 bits
 * for the fill count.
 */

/*
 * "Embedded" blkptr_t's don't actually point to a block, instead they
 * have a data payload embedded in the blkptr_t itself.  See the comment
 * in blkptr.c for more details.
 *
 * The blkptr_t is laid out as follows:
 *
 *	64	56	48	40	32	24	16	8	0
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 0	|      payload                                                  |
 * 1	|      payload                                                  |
 * 2	|      payload                                                  |
 * 3	|      payload                                                  |
 * 4	|      payload                                                  |
 * 5	|      payload                                                  |
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 6	|BDX|lvl| type	| etype |E| comp| PSIZE|              LSIZE	|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 7	|      payload                                                  |
 * 8	|      payload                                                  |
 * 9	|      payload                                                  |
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * a	|			logical birth txg			|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * b	|      payload                                                  |
 * c	|      payload                                                  |
 * d	|      payload                                                  |
 * e	|      payload                                                  |
 * f	|      payload                                                  |
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * Legend:
 *
 * payload		contains the embedded data
 * B (byteorder)	byteorder (endianness)
 * D (dedup)		padding (set to zero)
 * X			encryption (set to zero)
 * E (embedded)		set to one
 * lvl			indirection level
 * type			DMU object type
 * etype		how to interpret embedded data (BP_EMBEDDED_TYPE_*)
 * comp			compression function of payload
 * PSIZE		size of payload after compression, in bytes
 * LSIZE		logical size of payload, in bytes
 *			note that 25 bits is enough to store the largest
 *			"normal" BP's LSIZE (2^16 * 2^9) in bytes
 * log. birth		transaction group in which the block was logically born
 *
 * Note that LSIZE and PSIZE are stored in bytes, whereas for non-embedded
 * bp's they are stored in units of SPA_MINBLOCKSHIFT.
 * Generally, the generic BP_GET_*() macros can be used on embedded BP's.
 * The B, D, X, lvl, type, and comp fields are stored the same as with normal
 * BP's so the BP_SET_* macros can be used with them.  etype, PSIZE, LSIZE must
 * be set with the BPE_SET_* macros.  BP_SET_EMBEDDED() should be called before
 * other macros, as they assert that they are only used on BP's of the correct
 * "embedded-ness". Encrypted blkptr_t's cannot be embedded because they use
 * the payload space for encryption parameters (see the comment above on
 * how encryption parameters are stored).
 */

typedef struct blkptr {
	dva_t		blk_dva[SPA_DVAS_PER_BP]; /* Data Virtual Addresses */
	uint64_t	blk_prop;	/* size, compression, type, etc	    */
	uint64_t	blk_pad[2];	/* Extra space for the future	    */
	uint64_t	blk_phys_birth;	/* txg when block was allocated	    */
	uint64_t	blk_birth;	/* transaction group at birth	    */
	uint64_t	blk_fill;	/* fill count			    */
	zio_cksum_t	blk_cksum;	/* 256-bit checksum		    */
} blkptr_t;


/*
 * The uberblock version is incremented whenever an incompatible on-disk
 * format change is made to the SPA, DMU, or ZAP.
 *
 * Note: the first two fields should never be moved.  When a storage pool
 * is opened, the uberblock must be read off the disk before the version
 * can be checked.  If the ub_version field is moved, we may not detect
 * version mismatch.  If the ub_magic field is moved, applications that
 * expect the magic number in the first word won't work.
 */
#define	UBERBLOCK_MAGIC		0x00bab10c		/* oo-ba-bloc!	*/
#define	UBERBLOCK_SHIFT		10			/* up to 1K	*/
#define	MMP_MAGIC		0xa11cea11		/* all-see-all  */

struct uberblock {
	uint64_t	ub_magic;	/* UBERBLOCK_MAGIC		*/
	uint64_t	ub_version;	/* SPA_VERSION			*/
	uint64_t	ub_txg;		/* txg of last sync		*/
	uint64_t	ub_guid_sum;	/* sum of all vdev guids	*/
	uint64_t	ub_timestamp;	/* UTC time of last sync	*/
	blkptr_t	ub_rootbp;	/* MOS objset_phys_t		*/

	/* highest SPA_VERSION supported by software that wrote this txg */
	uint64_t	ub_software_version;

	/* Maybe missing in uberblocks we read, but always written */
	uint64_t	ub_mmp_magic;	/* MMP_MAGIC			*/
	uint64_t	ub_mmp_delay;	/* nanosec since last MMP write	*/
	uint64_t	ub_mmp_seq;	/* reserved for sequence number	*/
	uint64_t	ub_checkpoint_txg;
};

/*
 * VARIABLE-LENGTH (LARGE) DNODES
 *
 * The motivation for variable-length dnodes is to eliminate the overhead
 * associated with using spill blocks.  Spill blocks are used to store
 * system attribute data (i.e. file metadata) that does not fit in the
 * dnode's bonus buffer. By allowing a larger bonus buffer area the use of
 * a spill block can be avoided.  Spill blocks potentially incur an
 * additional read I/O for every dnode in a dnode block. As a worst case
 * example, reading 32 dnodes from a 16k dnode block and all of the spill
 * blocks could issue 33 separate reads. Now suppose those dnodes have size
 * 1024 and therefore don't need spill blocks. Then the worst case number
 * of blocks read is reduced to from 33 to two--one per dnode block.
 *
 * ZFS-on-Linux systems that make heavy use of extended attributes benefit
 * from this feature. In particular, ZFS-on-Linux supports the xattr=sa
 * dataset property which allows file extended attribute data to be stored
 * in the dnode bonus buffer as an alternative to the traditional
 * directory-based format. Workloads such as SELinux and the Lustre
 * distributed filesystem often store enough xattr data to force spill
 * blocks when xattr=sa is in effect. Large dnodes may therefore provide a
 * performance benefit to such systems. Other use cases that benefit from
 * this feature include files with large ACLs and symbolic links with long
 * target names.
 *
 * The size of a dnode may be a multiple of 512 bytes up to the size of a
 * dnode block (currently 16384 bytes). The dn_extra_slots field of the
 * on-disk dnode_phys_t structure describes the size of the physical dnode
 * on disk. The field represents how many "extra" dnode_phys_t slots a
 * dnode consumes in its dnode block. This convention results in a value of
 * 0 for 512 byte dnodes which preserves on-disk format compatibility with
 * older software which doesn't support large dnodes.
 *
 * Similarly, the in-memory dnode_t structure has a dn_num_slots field
 * to represent the total number of dnode_phys_t slots consumed on disk.
 * Thus dn->dn_num_slots is 1 greater than the corresponding
 * dnp->dn_extra_slots. This difference in convention was adopted
 * because, unlike on-disk structures, backward compatibility is not a
 * concern for in-memory objects, so we used a more natural way to
 * represent size for a dnode_t.
 *
 * The default size for newly created dnodes is determined by the value of
 * the "dnodesize" dataset property. By default the property is set to
 * "legacy" which is compatible with older software. Setting the property
 * to "auto" will allow the filesystem to choose the most suitable dnode
 * size. Currently this just sets the default dnode size to 1k, but future
 * code improvements could dynamically choose a size based on observed
 * workload patterns. Dnodes of varying sizes can coexist within the same
 * dataset and even within the same dnode block.
 */

/*
 * Fixed constants.
 */
#define	DNODE_SHIFT		9	/* 512 bytes */
#define	DN_MIN_INDBLKSHIFT	12	/* 4k */

/*
 * If we ever increase this value beyond 20, we need to revisit all logic that
 * does x << level * ebps to handle overflow.  With a 1M indirect block size,
 * 4 levels of indirect blocks would not be able to guarantee addressing an
 * entire object, so 5 levels will be used, but 5 * (20 - 7) = 65.
 */
#define	DN_MAX_INDBLKSHIFT	17	/* 128k */
#define	DNODE_BLOCK_SHIFT	14	/* 16k */
#define	DNODE_CORE_SIZE		64	/* 64 bytes for dnode sans blkptrs */
#define	DN_MAX_OBJECT_SHIFT	48	/* 256 trillion (zfs_fid_t limit) */
#define	DN_MAX_OFFSET_SHIFT	64	/* 2^64 bytes in a dnode */

/*
 * Derived constants.
 */
#define	DNODE_MIN_SIZE		(1 << DNODE_SHIFT)
#define	DNODE_MAX_SIZE		(1 << DNODE_BLOCK_SHIFT)
#define	DNODE_BLOCK_SIZE	(1 << DNODE_BLOCK_SHIFT)
#define	DNODE_MIN_SLOTS		(DNODE_MIN_SIZE >> DNODE_SHIFT)
#define	DNODE_MAX_SLOTS		(DNODE_MAX_SIZE >> DNODE_SHIFT)
#define	DN_BONUS_SIZE(dnsize)	((dnsize) - DNODE_CORE_SIZE - \
	(1 << SPA_BLKPTRSHIFT))
#define	DN_SLOTS_TO_BONUSLEN(slots)	DN_BONUS_SIZE((slots) << DNODE_SHIFT)
#define	DN_OLD_MAX_BONUSLEN	(DN_BONUS_SIZE(DNODE_MIN_SIZE))
#define	DN_MAX_NBLKPTR	((DNODE_MIN_SIZE - DNODE_CORE_SIZE) >> SPA_BLKPTRSHIFT)
#define	DN_MAX_OBJECT	(1ULL << DN_MAX_OBJECT_SHIFT)
#define	DN_ZERO_BONUSLEN	(DN_BONUS_SIZE(DNODE_MAX_SIZE) + 1)
#define	DN_KILL_SPILLBLK (1)

#define	DN_SLOT_UNINIT		((void *)NULL)	/* Uninitialized */
#define	DN_SLOT_FREE		((void *)1UL)	/* Free slot */
#define	DN_SLOT_ALLOCATED	((void *)2UL)	/* Allocated slot */
#define	DN_SLOT_INTERIOR	((void *)3UL)	/* Interior allocated slot */
#define	DN_SLOT_IS_PTR(dn)	((void *)dn > DN_SLOT_INTERIOR)
#define	DN_SLOT_IS_VALID(dn)	((void *)dn != NULL)

#define	DNODES_PER_BLOCK_SHIFT	(DNODE_BLOCK_SHIFT - DNODE_SHIFT)
#define	DNODES_PER_BLOCK	(1ULL << DNODES_PER_BLOCK_SHIFT)

typedef enum dmu_object_type {
	DMU_OT_NONE,                    /* 0: */
	/* general: */
	DMU_OT_OBJECT_DIRECTORY,	/* 1:ZAP */
	DMU_OT_OBJECT_ARRAY,		/* 2:UINT64 */
	DMU_OT_PACKED_NVLIST,		/* 3:UINT8 (XDR by nvlist_pack/unpack) */
	DMU_OT_PACKED_NVLIST_SIZE,	/* 4:UINT64 */
	DMU_OT_BPOBJ,			/* 5:UINT64 */
	DMU_OT_BPOBJ_HDR,		/* 6:UINT64 */
	/* spa: */
	DMU_OT_SPACE_MAP_HEADER,	/* 7:UINT64 */
	DMU_OT_SPACE_MAP,		/* 8:UINT64 */
	/* zil: */
	DMU_OT_INTENT_LOG,		/* 9:UINT64 */
	/* dmu: */
	DMU_OT_DNODE,			/* 10:DNODE */
	DMU_OT_OBJSET,			/* 11:OBJSET */
	/* dsl: */
	DMU_OT_DSL_DIR,			/* 12:UINT64 */
	DMU_OT_DSL_DIR_CHILD_MAP,	/* 13:ZAP */
	DMU_OT_DSL_DS_SNAP_MAP,		/* 14:ZAP */
	DMU_OT_DSL_PROPS,		/* 15:ZAP */
	DMU_OT_DSL_DATASET,		/* 16:UINT64 */
	/* zpl: */
	DMU_OT_ZNODE,			/* 17:ZNODE */
	DMU_OT_OLDACL,			/* 18:Old ACL */
	DMU_OT_PLAIN_FILE_CONTENTS,	/* 19:UINT8 */
	DMU_OT_DIRECTORY_CONTENTS,	/* 20:ZAP */
	DMU_OT_MASTER_NODE,		/* 21:ZAP */
	DMU_OT_UNLINKED_SET,		/* 22:ZAP */
	/* zvol: */
	DMU_OT_ZVOL,			/* 23:UINT8 */
	DMU_OT_ZVOL_PROP,		/* 24:ZAP */
	/* other; for testing only! */
	DMU_OT_PLAIN_OTHER,		/* 25:UINT8 */
	DMU_OT_UINT64_OTHER,		/* 26:UINT64 */
	DMU_OT_ZAP_OTHER,		/* 27:ZAP */
	/* new object types: */
	DMU_OT_ERROR_LOG,		/* 28:ZAP */
	DMU_OT_SPA_HISTORY,		/* 29:UINT8 */
	DMU_OT_SPA_HISTORY_OFFSETS,	/* 30:spa_his_phys_t */
	DMU_OT_POOL_PROPS,		/* 31:ZAP */
	DMU_OT_DSL_PERMS,		/* 32:ZAP */
	DMU_OT_ACL,			/* 33:ACL */
	DMU_OT_SYSACL,			/* 34:SYSACL */
	DMU_OT_FUID,			/* 35:FUID table (Packed NVLIST UINT8) */
	DMU_OT_FUID_SIZE,		/* 36:FUID table size UINT64 */
	DMU_OT_NEXT_CLONES,		/* 37:ZAP */
	DMU_OT_SCAN_QUEUE,		/* 38:ZAP */
	DMU_OT_USERGROUP_USED,		/* 39:ZAP */
	DMU_OT_USERGROUP_QUOTA,		/* 40:ZAP */
	DMU_OT_USERREFS,		/* 41:ZAP */
	DMU_OT_DDT_ZAP,			/* 42:ZAP */
	DMU_OT_DDT_STATS,		/* 43:ZAP */
	DMU_OT_SA,			/* 44:System attr */
	DMU_OT_SA_MASTER_NODE,		/* 45:ZAP */
	DMU_OT_SA_ATTR_REGISTRATION,	/* 46:ZAP */
	DMU_OT_SA_ATTR_LAYOUTS,		/* 47:ZAP */
	DMU_OT_SCAN_XLATE,		/* 48:ZAP */
	DMU_OT_DEDUP,			/* 49:fake dedup BP from ddt_bp_create() */
	DMU_OT_DEADLIST,		/* 50:ZAP */
	DMU_OT_DEADLIST_HDR,		/* 51:UINT64 */
	DMU_OT_DSL_CLONES,		/* 52:ZAP */
	DMU_OT_BPOBJ_SUBOBJ,		/* 53:UINT64 */
	/*
	 * Do not allocate new object types here. Doing so makes the on-disk
	 * format incompatible with any other format that uses the same object
	 * type number.
	 *
	 * When creating an object which does not have one of the above types
	 * use the DMU_OTN_* type with the correct byteswap and metadata
	 * values.
	 *
	 * The DMU_OTN_* types do not have entries in the dmu_ot table,
	 * use the DMU_OT_IS_METDATA() and DMU_OT_BYTESWAP() macros instead
	 * of indexing into dmu_ot directly (this works for both DMU_OT_* types
	 * and DMU_OTN_* types).
	 */
	DMU_OT_NUMTYPES,

	/*
	 * Names for valid types declared with DMU_OT().
	 */
	DMU_OTN_ZAP_DATA = 196,

} dmu_object_type_t;

typedef struct dnode_phys {
	uint8_t dn_type;		/* dmu_object_type_t */
	uint8_t dn_indblkshift;		/* ln2(indirect block size) */
	uint8_t dn_nlevels;		/* 1=dn_blkptr->data blocks */
	uint8_t dn_nblkptr;		/* length of dn_blkptr */
	uint8_t dn_bonustype;		/* type of data in bonus buffer */
	uint8_t	dn_checksum;		/* ZIO_CHECKSUM type */
	uint8_t	dn_compress;		/* ZIO_COMPRESS type */
	uint8_t dn_flags;		/* DNODE_FLAG_* */
	uint16_t dn_datablkszsec;	/* data block size in 512b sectors */
	uint16_t dn_bonuslen;		/* length of dn_bonus */
	uint8_t dn_extra_slots;		/* # of subsequent slots consumed */
	uint8_t dn_pad2[3];

	/* accounting is protected by dn_dirty_mtx */
	uint64_t dn_maxblkid;		/* largest allocated block ID */
	uint64_t dn_used;		/* bytes (or sectors) of disk space */

	/*
	 * Both dn_pad2 and dn_pad3 are protected by the block's MAC. This
	 * allows us to protect any fields that might be added here in the
	 * future. In either case, developers will want to check
	 * zio_crypt_init_uios_dnode() to ensure the new field is being
	 * protected properly.
	 */
	uint64_t dn_pad3[4];

	/*
	 * The tail region is 448 bytes for a 512 byte dnode, and
	 * correspondingly larger for larger dnode sizes. The spill
	 * block pointer, when present, is always at the end of the tail
	 * region. There are three ways this space may be used, using
	 * a 512 byte dnode for this diagram:
	 *
	 * 0       64      128     192     256     320     384     448 (offset)
	 * +---------------+---------------+---------------+-------+
	 * | dn_blkptr[0]  | dn_blkptr[1]  | dn_blkptr[2]  | /     |
	 * +---------------+---------------+---------------+-------+
	 * | dn_blkptr[0]  | dn_bonus[0..319]                      |
	 * +---------------+-----------------------+---------------+
	 * | dn_blkptr[0]  | dn_bonus[0..191]      | dn_spill      |
	 * +---------------+-----------------------+---------------+
	 */
	union {
		blkptr_t dn_blkptr[1+DN_OLD_MAX_BONUSLEN/sizeof (blkptr_t)];
		struct {
			blkptr_t __dn_ignore1;
			uint8_t dn_bonus[DN_OLD_MAX_BONUSLEN];
		};
		struct {
			blkptr_t __dn_ignore2;
			uint8_t __dn_ignore3[DN_OLD_MAX_BONUSLEN -
			    sizeof (blkptr_t)];
			blkptr_t dn_spill;
		};
	};
} dnode_phys_t;

typedef enum dd_used {
	DD_USED_HEAD,
	DD_USED_SNAP,
	DD_USED_CHILD,
	DD_USED_CHILD_RSRV,
	DD_USED_REFRSRV,
	DD_USED_NUM
} dd_used_t;

typedef struct dsl_dir_phys {
	uint64_t dd_creation_time; /* not actually used */
	uint64_t dd_head_dataset_obj;
	uint64_t dd_parent_obj;
	uint64_t dd_origin_obj;
	uint64_t dd_child_dir_zapobj;
	/*
	 * how much space our children are accounting for; for leaf
	 * datasets, == physical space used by fs + snaps
	 */
	uint64_t dd_used_bytes;
	uint64_t dd_compressed_bytes;
	uint64_t dd_uncompressed_bytes;
	/* Administrative quota setting */
	uint64_t dd_quota;
	/* Administrative reservation setting */
	uint64_t dd_reserved;
	uint64_t dd_props_zapobj;
	uint64_t dd_deleg_zapobj; /* dataset delegation permissions */
	uint64_t dd_flags;
	uint64_t dd_used_breakdown[DD_USED_NUM];
	uint64_t dd_clones; /* dsl_dir objects */
	uint64_t dd_pad[13]; /* pad out to 256 bytes for good measure */
} dsl_dir_phys_t;

typedef struct dsl_dataset_phys {
	uint64_t ds_dir_obj;		/* DMU_OT_DSL_DIR */
	uint64_t ds_prev_snap_obj;	/* DMU_OT_DSL_DATASET */
	uint64_t ds_prev_snap_txg;
	uint64_t ds_next_snap_obj;	/* DMU_OT_DSL_DATASET */
	uint64_t ds_snapnames_zapobj;	/* DMU_OT_DSL_DS_SNAP_MAP 0 for snaps */
	uint64_t ds_num_children;	/* clone/snap children; ==0 for head */
	uint64_t ds_creation_time;	/* seconds since 1970 */
	uint64_t ds_creation_txg;
	uint64_t ds_deadlist_obj;	/* DMU_OT_DEADLIST */
	/*
	 * ds_referenced_bytes, ds_compressed_bytes, and ds_uncompressed_bytes
	 * include all blocks referenced by this dataset, including those
	 * shared with any other datasets.
	 */
	uint64_t ds_referenced_bytes;
	uint64_t ds_compressed_bytes;
	uint64_t ds_uncompressed_bytes;
	uint64_t ds_unique_bytes;	/* only relevant to snapshots */
	/*
	 * The ds_fsid_guid is a 56-bit ID that can change to avoid
	 * collisions.  The ds_guid is a 64-bit ID that will never
	 * change, so there is a small probability that it will collide.
	 */
	uint64_t ds_fsid_guid;
	uint64_t ds_guid;
	uint64_t ds_flags;		/* DS_FLAG_* */
	blkptr_t ds_bp;
	uint64_t ds_next_clones_obj;	/* DMU_OT_DSL_CLONES */
	uint64_t ds_props_obj;		/* DMU_OT_DSL_PROPS for snaps */
	uint64_t ds_userrefs_obj;	/* DMU_OT_USERREFS */
	uint64_t ds_pad[5]; /* pad out to 320 bytes for good measure */
} dsl_dataset_phys_t;

typedef struct zio_eck {
	uint64_t	zec_magic;	/* for validation, endianness	*/
	zio_cksum_t	zec_cksum;	/* 256-bit checksum		*/
} zio_eck_t;

#define	VDEV_PHYS_SIZE		(112 << 10)
#define	VDEV_PAD_SIZE		(8 << 10)
#define	VDEV_UBERBLOCK_RING	(128 << 10)

typedef struct vdev_phys {
	char		vp_nvlist[VDEV_PHYS_SIZE - sizeof (zio_eck_t)];
	zio_eck_t	vp_zbt;
} vdev_phys_t;

typedef struct vdev_label {
	char		vl_pad1[VDEV_PAD_SIZE];			/*  8K */
	char		vl_pad2[VDEV_PAD_SIZE];			/*  8K */
	vdev_phys_t	vl_vdev_phys;				/* 112K	*/
	char		vl_uberblock[VDEV_UBERBLOCK_RING];	/* 128K	*/
} vdev_label_t;							/* 256K total */

/*
 * TAKE NOTE:
 * If zap_phys_t is modified, zap_byteswap() must be modified.
 */
typedef struct zap_phys {
	uint64_t zap_block_type;	/* ZBT_HEADER */
	uint64_t zap_magic;		/* ZAP_MAGIC */

	struct zap_table_phys {
		uint64_t zt_blk;	/* starting block number */
		uint64_t zt_numblks;	/* number of blocks */
		uint64_t zt_shift;	/* bits to index it */
		uint64_t zt_nextblk;	/* next (larger) copy start block */
		uint64_t zt_blks_copied; /* number source blocks copied */
	} zap_ptrtbl;

	uint64_t zap_freeblk;		/* the next free block */
	uint64_t zap_num_leafs;		/* number of leafs */
	uint64_t zap_num_entries;	/* number of entries */
	uint64_t zap_salt;		/* salt to stir into hash function */
	uint64_t zap_normflags;		/* flags for u8_textprep_str() */
	uint64_t zap_flags;		/* zap_flags_t */
	/*
	 * This structure is followed by padding, and then the embedded
	 * pointer table.  The embedded pointer table takes up second
	 * half of the block.  It is accessed using the
	 * ZAP_EMBEDDED_PTRTBL_ENT() macro.
	 */
} zap_phys_t;

/*
 * TAKE NOTE:
 * If zap_leaf_phys_t is modified, zap_leaf_byteswap() must be modified.
 */
typedef struct zap_leaf_phys {
	struct zap_leaf_header {
		/* Public to ZAP */
		uint64_t lh_block_type;		/* ZBT_LEAF */
		uint64_t lh_pad1;
		uint64_t lh_prefix;		/* hash prefix of this leaf */
		uint32_t lh_magic;		/* ZAP_LEAF_MAGIC */
		uint16_t lh_nfree;		/* number free chunks */
		uint16_t lh_nentries;		/* number of entries */
		uint16_t lh_prefix_len;		/* num bits used to id this */

		/* Private to zap_leaf */
		uint16_t lh_freelist;		/* chunk head of free list */
		uint8_t lh_flags;		/* ZLF_* flags */
		uint8_t lh_pad2[11];
	} l_hdr; /* 2 24-byte chunks */

	/*
	 * The header is followed by a hash table with
	 * ZAP_LEAF_HASH_NUMENTRIES(zap) entries.  The hash table is
	 * followed by an array of ZAP_LEAF_NUMCHUNKS(zap)
	 * zap_leaf_chunk structures.  These structures are accessed
	 * with the ZAP_LEAF_CHUNK() macro.
	 */

	uint16_t l_hash[1];
} zap_leaf_phys_t;

#endif
