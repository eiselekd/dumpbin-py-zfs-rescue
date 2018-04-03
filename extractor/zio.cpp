#include "defs.h"
#include <assert.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "zio.h"

#undef MAX
#undef MIN
#undef roundup
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define B_FALSE 0

typedef uint32_t uint_t ;

typedef struct raidz_col {
	uint64_t rc_devidx;		/* child device index for I/O */
	uint64_t rc_offset;		/* device offset */
	uint64_t rc_size;		/* I/O size */
	abd_t *rc_abd;			/* I/O data */
	void *rc_gdata;			/* used to store the "good" version */
	int rc_error;			/* I/O error for this device */
	uint8_t rc_tried;		/* Did we attempt this I/O column? */
	uint8_t rc_skipped;		/* Did we skip this I/O column? */
} raidz_col_t;

typedef struct raidz_map {
	uint64_t rm_cols;		/* Regular column count */
	uint64_t rm_scols;		/* Count including skipped columns */
	uint64_t rm_bigcols;		/* Number of oversized columns */
	uint64_t rm_asize;		/* Actual total I/O size */
	uint64_t rm_missingdata;	/* Count of missing data devices */
	uint64_t rm_missingparity;	/* Count of missing parity devices */
	uint64_t rm_firstdatacol;	/* First data column/parity count */
	uint64_t rm_nskip;		/* Skipped sectors for padding */
	uint64_t rm_skipstart;		/* Column index of padding start */
	abd_t *rm_abd_copy;		/* rm_asize-buffer of copied data */
	uintptr_t rm_reports;		/* # of referencing checksum reports */
	uint8_t	rm_freed;		/* map no longer has referencing ZIO */
	uint8_t	rm_ecksuminjected;	/* checksum error was injected */
	raidz_col_t rm_col[1];		/* Flexible array of I/O columns */
} raidz_map_t;

//#define ASSERT3U(l,op,r) assert(l op r);
//#define ASSERT(l) assert(l);

typedef struct zio {
	uint64_t	io_offset;
	uint64_t	io_size;
	struct abd	*io_abd;
	void		*io_vsd;
} zio_t;

void abd_free(abd_t *abd) {
	if (abd->abd_flags & ABD_FLAG_OWNER) {
		free(abd->abd_buf);
	}
	free(abd);
}

abd_t *
abd_alloc_linear(uint64_t size, int is_metadata)
{
	(void)is_metadata;
	abd_t *c = (abd_t *)malloc(sizeof(abd_t));
	c->abd_flags = (abd_flags_t) (ABD_FLAG_OWNER | ABD_FLAG_LINEAR);
	c->abd_parent = 0;
	c->abd_size = size;
	c->abd_buf = malloc(size);;
	return c;
}

abd_t *
abd_get_offset_size(abd_t *sabd, size_t off, uint64_t size)
{
	abd_t *c = (abd_t *)malloc(sizeof(abd_t));
	c->abd_flags = (abd_flags_t) 0;
	c->abd_parent = sabd;
	c->abd_size = size;
	c->abd_buf = ((char*)c->abd_parent->abd_buf) + off;
	return c;
}

/*
 * Divides the IO evenly across all child vdevs; usually, dcols is
 * the number of children in the target vdev.
 *
 * Avoid inlining the function to keep vdev_raidz_io_start(), which
 * is this functions only caller, as small as possible on the stack.
 */
raidz_map_t *
vdev_raidz_map_alloc(zio_t *zio, uint64_t ashift, uint64_t dcols,
    uint64_t nparity)
{
	raidz_map_t *rm;
	/* The starting RAIDZ (parent) vdev sector of the block. */
	uint64_t b = zio->io_offset >> ashift;
	/* The zio's size in units of the vdev's minimum sector size. */
	uint64_t s = zio->io_size >> ashift;
	/* The first column for this stripe. */
	uint64_t f = b % dcols;
	/* The starting byte offset on each child vdev. */
	uint64_t o = (b / dcols) << ashift;
	uint64_t q, r, c, bc, col, acols, scols, coff, devidx, asize, tot;
	uint64_t off = 0;

	printf("[+] : 0x%llx:0x%lx ashift:%lu,%lu,%lu\n", (long long unsigned)zio->io_offset, (long unsigned)zio->io_size, ashift, dcols, nparity);

	/*
	 * "Quotient": The number of data sectors for this stripe on all but
	 * the "big column" child vdevs that also contain "remainder" data.
	 */
	q = s / (dcols - nparity);

	/*
	 * "Remainder": The number of partial stripe data sectors in this I/O.
	 * This will add a sector to some, but not all, child vdevs.
	 */
	r = s - q * (dcols - nparity);

	/* The number of "big columns" - those which contain remainder data. */
	bc = (r == 0 ? 0 : r + nparity);

	/*
	 * The total number of data and parity sectors associated with
	 * this I/O.
	 */
	tot = s + nparity * (q + (r == 0 ? 0 : 1));

	/* acols: The columns that will be accessed. */
	/* scols: The columns that will be accessed or skipped. */
	if (q == 0) {
		/* Our I/O request doesn't span all child vdevs. */
		acols = bc;
		scols = MIN(dcols, roundup(bc, nparity + 1));
	} else {
		acols = dcols;
		scols = dcols;
	}

	ASSERT3U(acols, <=, scols);

	rm = (raidz_map_t *) malloc(sizeof(raidz_map_t) + ((scols-1) * sizeof(raidz_col_t)));

	rm->rm_cols = acols;
	rm->rm_scols = scols;
	rm->rm_bigcols = bc;
	rm->rm_skipstart = bc;
	rm->rm_missingdata = 0;
	rm->rm_missingparity = 0;
	rm->rm_firstdatacol = nparity;
	rm->rm_abd_copy = NULL;
	rm->rm_reports = 0;
	rm->rm_freed = 0;
	rm->rm_ecksuminjected = 0;

	asize = 0;

	for (c = 0; c < scols; c++) {
		col = f + c;
		coff = o;
		if (col >= dcols) {
			col -= dcols;
			coff += 1ULL << ashift;
		}
		rm->rm_col[c].rc_devidx = col;
		rm->rm_col[c].rc_offset = coff;
		rm->rm_col[c].rc_abd = NULL;
		rm->rm_col[c].rc_gdata = NULL;
		rm->rm_col[c].rc_error = 0;
		rm->rm_col[c].rc_tried = 0;
		rm->rm_col[c].rc_skipped = 0;

		if (c >= acols)
			rm->rm_col[c].rc_size = 0;
		else if (c < bc)
			rm->rm_col[c].rc_size = (q + 1) << ashift;
		else
			rm->rm_col[c].rc_size = q << ashift;

		asize += rm->rm_col[c].rc_size;
	}

	ASSERT3U(asize, ==, tot << ashift);
	rm->rm_asize = roundup(asize, (nparity + 1) << ashift);
	rm->rm_nskip = roundup(tot, nparity + 1) - tot;
	ASSERT3U(rm->rm_asize - asize, ==, rm->rm_nskip << ashift);
	ASSERT3U(rm->rm_nskip, <=, nparity);

	for (c = 0; c < rm->rm_firstdatacol; c++)
		rm->rm_col[c].rc_abd =
		    abd_alloc_linear(rm->rm_col[c].rc_size, B_FALSE);

	rm->rm_col[c].rc_abd = abd_get_offset_size(zio->io_abd, 0,
	    rm->rm_col[c].rc_size);
	off = rm->rm_col[c].rc_size;

	for (c = c + 1; c < acols; c++) {
		rm->rm_col[c].rc_abd = abd_get_offset_size(zio->io_abd, off,
		    rm->rm_col[c].rc_size);
		off += rm->rm_col[c].rc_size;
	}

	/*
	 * If all data stored spans all columns, there's a danger that parity
	 * will always be on the same device and, since parity isn't read
	 * during normal operation, that that device's I/O bandwidth won't be
	 * used effectively. We therefore switch the parity every 1MB.
	 *
	 * ... at least that was, ostensibly, the theory. As a practical
	 * matter unless we juggle the parity between all devices evenly, we
	 * won't see any benefit. Further, occasional writes that aren't a
	 * multiple of the LCM of the number of children and the minimum
	 * stripe width are sufficient to avoid pessimal behavior.
	 * Unfortunately, this decision created an implicit on-disk format
	 * requirement that we need to support for all eternity, but only
	 * for single-parity RAID-Z.
	 *
	 * If we intend to skip a sector in the zeroth column for padding
	 * we must make sure to note this swap. We will never intend to
	 * skip the first column since at least one data and one parity
	 * column must appear in each row.
	 */
	ASSERT(rm->rm_cols >= 2);
	ASSERT(rm->rm_col[0].rc_size == rm->rm_col[1].rc_size);

	if (rm->rm_firstdatacol == 1 && (zio->io_offset & (1ULL << 20))) {
		devidx = rm->rm_col[0].rc_devidx;
		o = rm->rm_col[0].rc_offset;
		rm->rm_col[0].rc_devidx = rm->rm_col[1].rc_devidx;
		rm->rm_col[0].rc_offset = rm->rm_col[1].rc_offset;
		rm->rm_col[1].rc_devidx = devidx;
		rm->rm_col[1].rc_offset = o;

		if (rm->rm_skipstart == 0)
			rm->rm_skipstart = 1;
	}

	return (rm);
}

const char *DMU_TYPE_DESC[] = {
    "unallocated",              // 0
    "object directory",         // 1
    "object array",             // 2
    "packed nvlist",            // 3
    "packed nvlist size",       // 4
    "bpobj",                    // 5
    "bpobj header",             // 6
    "SPA space map header",     // 7
    "SPA space map",            // 8
    "ZIL intent log",           // 9
    "DMU dnode",                // 10
    "DMU objset",               // 11
    "DSL directory",            // 12
    "DSL directory child map",  // 13
    "DSL dataset snap map",     // 14
    "DSL props",                // 15
    "DSL dataset",              // 16
    "ZFS znode",                // 17
    "ZFS V0 ACL",               // 18
    "ZFS plain file",           // 19
    "ZFS directory",            // 20
    "ZFS master node",          // 21
    "ZFS delete queue",         // 22
    "zvol object",              // 23
    "zvol prop",                // 24
    "other uint8[]",            // 25
    "other uint64[]",           // 26
    "other ZAP",                // 27
    "persistent error log",     // 28
    "SPA history",              // 29
    "SPA history offsets",      // 30
    "Pool properties",          // 31
    "DSL permissions",          // 32
    "ZFS ACL",                  // 33
    "ZFS SYSACL",               // 34
    "FUID table",               // 35
    "FUID table size",          // 36
    "DSL dataset next clones",  // 37
    "scan work queue",          // 38
    "ZFS user/group used",      // 39
    "ZFS user/group quota",     // 40
    "snapshot refcount tags",   // 41
    "DDT ZAP algorithm",        // 42
    "DDT statistics",           // 43
    "System attributes",        // 44
    "SA master node",           // 45
    "SA attr registration",     // 46
    "SA attr layouts",          // 47
    "scan translations",        // 48
    "deduplicated block",       // 49
    "DSL deadlist map",         // 50
    "DSL deadlist map hdr",     // 51
    "DSL dir clones",           // 52
    "bpobj subobj"              // 53
};

void
vdev_raidz_free(raidz_map_t *rm)
{
	uint i;
	for (i = 0; i < rm->rm_cols; i++) {
		abd_free(rm->rm_col[i].rc_abd);
	}
	free(rm);
}

Raidz1Device::Raidz1Device(std::vector<std::string> &vdevs) : vdevs(vdevs) {
	assert(vdevs.size() == DCOLS);


};

abd_t *Raidz1Device::read_physical(uint64_t offset, uint64_t psize)
{
 	zio_t io; uint i;
	uint64_t ashift = 12;
	raidz_map_t *rm;
	abd_t *c;

	io.io_offset = offset;
	io.io_size = psize;
	io.io_abd = c = abd_alloc_linear(psize, B_FALSE);

	rm = vdev_raidz_map_alloc(&io, ASHIFT, DCOLS, NPARITY);

	for (i = 0; i < rm->rm_cols; i++) {
		read_at(rm->rm_col[i].rc_abd, rm->rm_col[i].rc_devidx, rm->rm_col[i].rc_offset, rm->rm_col[i].rc_size);
	}

	vdev_raidz_free(rm);
	return c;
}

int Raidz1Device::read_at(abd_t *c, int vdev, uint64_t offset, uint64_t psize)
{
	int fd;
	fd = open(vdevs[vdev].c_str(), O_RDONLY);
	pread64(fd, c->abd_buf, psize, offset);
	close(fd);
}

int Raidz1Device::loadLabel(int dev, int labidx)
{
	vdev_label_t l;
	abd_t abd;
	abd.abd_buf = &l;
	uint64_t offset = 0;
	uint64_t psize = sizeof(vdev_label_t);
	struct uberblock *u;
	int usz = 1 << ASHIFT; int i;
	int ucnt = sizeof(l.vl_uberblock) / usz;
	read_at(&abd, dev, offset, psize);
	for (i = 0; i < ucnt; i++) {
		u = (struct uberblock *)&(l.vl_uberblock[i*usz]);
		tgxs[u->ub_txg] = *u;
	}
}

int Raidz1Device::loadMos(uint64_t tgx)
{
	struct uberblock u;
	u = tgxs[tgx];

	DNode *d = loadDnode(u.ub_rootbp);
	assert(d->type() == DMU_OT_DSL_DATASET);
	DSLDataset *mos = static_cast<DSLDataset*>(d);

	for (uint64_t i = 0; i < mos->len(); i++) {
		DNode *e = (*mos)[i];
		if (e && e->type() == DMU_OT_DSL_DATASET) {
			datasets[i] = e;
		}
	}

	DNode *root_ds = (*mos)[1];
	assert(root_ds->type() == DMU_OT_OBJECT_DIRECTORY);
	Zap *root_ds_z = root_ds->zap();
	uint64_t rootdir_id = (*root_ds_z)["root_dataset"];
	DNode *rdir = (*mos)[rootdir_id];

}

DNode *Raidz1Device::loadDnode(blkptr_t &p)
{
	dnode_phys_t *d;
	abd_t *c;
	c = loadBlkPtr(p);
	if (!c)
		return 0;
	d = (dnode_phys_t *)c->abd_buf;
	switch (d->dn_type) {
	case DMU_OT_DSL_DIR:

		break;
	case DMU_OT_DSL_DATASET:

		break;
	}

	return 0;
}

abd_t *Raidz1Device::loadBlkPtr(blkptr_t &p)
{
	abd_t *c = 0;
	for (int i = 0; i < 3; i++) {
		dva d = p.blk_dva[i];
		uint64_t asize;
		uint64_t offset;
		asize = (d.dva_word[0] & 0xffffffUL) << 9;
		offset = d.dva_word[1] & 0x7fffffffffffffffUL;
		c = read_physical(offset, asize);
		if (c) {
			return c;
		}
	}
	return 0;
}



int Raidz1Device::loadChildDS(uint64_t dsid)
{

}


/*
  Local Variables:
  mode:c++
  c-file-style:"linux"
  End:
*/
