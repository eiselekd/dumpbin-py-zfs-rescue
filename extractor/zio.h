#ifndef _ZIO_H_DEF_
#define _ZIO_H_DEF_
#include <vector>
#include <map>
#include <string>
#include "zap.h"

class bytearray {
    int l;
};

#define NPARITY 1
#define DCOLS 3
#define ASHIFT 12

typedef enum abd_flags {
	ABD_FLAG_LINEAR	= 1 << 0,	/* is buffer linear (or scattered)? */
	ABD_FLAG_OWNER	= 1 << 1,	/* does it own its data buffers? */
	ABD_FLAG_META	= 1 << 2,	/* does this represent FS metadata? */
	ABD_FLAG_MULTI_ZONE  = 1 << 3,	/* pages split over memory zones */
	ABD_FLAG_MULTI_CHUNK = 1 << 4	/* pages split over multiple chunks */
} abd_flags_t;

typedef struct abd {
	abd_flags_t	abd_flags;
	uint64_t	abd_size;	/* excludes scattered abd_offset */
	struct abd	*abd_parent;
	void		*abd_buf;
} abd_t;

class DNode {
public:
    DNode(dnode_phys_t &d);
    dmu_object_type_t type() { return (dmu_object_type_t) d.dn_type; }
    Zap *zap();

    dnode_phys_t d;
};

class DSLDataset : public DNode {
public:
    DNode *operator[] (uint64_t id) {
	return 0;
    }

    uint64_t len() { return cnt; };

    uint64_t cnt;
};

class DSLDirectory : public DNode {

};


class Raidz1Device {
public:
    Raidz1Device(std::vector<std::string> &vdevs);

    abd_t *read_physical(uint64_t offset, uint64_t psize);

    int read_at(abd_t *c, int vdev, uint64_t offset, uint64_t psize);


    int loadLabel(int dev, int labidx);
    int loadMos(uint64_t tgx);
    int loadChildDS(uint64_t tgx);

    //dnode
    DNode *loadDnode(blkptr_t &p);
    abd_t *loadBlkPtr(blkptr_t &p);

    std::vector<std::string> vdevs;
    std::map<uint64_t,struct uberblock> tgxs;
    std::map<uint64_t,DNode *> datasets;
};


#endif
