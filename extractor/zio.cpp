
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

typedef enum abd_flags {
	ABD_FLAG_LINEAR	= 1 << 0,	/* is buffer linear (or scattered)? */
	ABD_FLAG_OWNER	= 1 << 1,	/* does it own its data buffers? */
	ABD_FLAG_META	= 1 << 2,	/* does this represent FS metadata? */
	ABD_FLAG_MULTI_ZONE  = 1 << 3,	/* pages split over memory zones */
	ABD_FLAG_MULTI_CHUNK = 1 << 4	/* pages split over multiple chunks */
} abd_flags_t;

typedef uint32_t uint_t ;

typedef struct abd {
	abd_flags_t	abd_flags;
	uint_t		abd_size;	/* excludes scattered abd_offset */
	struct abd	*abd_parent;
	union {
		struct abd_scatter {
			uint_t		abd_offset;
			uint_t		abd_nents;
			struct scatterlist *abd_sgl;
		} abd_scatter;
		struct abd_linear {
			void		*abd_buf;
		} abd_linear;
	} abd_u;
} abd_t;

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

#define ASSERT3U(l,op,r) assert(l op r);
#define ASSERT(l) assert(l);

typedef struct zio {
	uint64_t	io_offset;
	uint64_t	io_size;
	struct abd	*io_abd;
	void		*io_vsd;
} zio_t;

typedef enum boolean { B_FALSE, B_TRUE } boolean_t;

abd_t *
abd_alloc_linear(size_t size, boolean_t is_metadata)
{
	return 0;
}

abd_t *
abd_get_offset_size(abd_t *sabd, size_t off, size_t size)
{
	ASSERT3U(off + size, <=, sabd->abd_size);
	return 0;
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

	printf("[+] : 0x%llx:0x%lx ashift:%d,%d,%d\n", (long long unsigned)zio->io_offset, (long unsigned)zio->io_size, ashift, dcols, nparity);

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

	rm = malloc(offsetof(raidz_map_t, rm_col[scols]));

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

	/* init RAIDZ parity ops */

	return (rm);
}

/*
  Local Variables:
  mode:c++
  c-file-style:"linux"
  End:
*/
