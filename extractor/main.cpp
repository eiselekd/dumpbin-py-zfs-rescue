/* usage:
 * extractor.exe <DVA> <path>
 * given a <DVA> i.e. 0:4000:2000 and a <path>, it will
 * extract the data of the zfs-file dnode pointed to by DVA
 * into file <path>, the value of DVA should be extracted  in
 * advance by py-zfs-rescue or similar.
 *
 */
#include "config.h"
#include <string.h>

int verbose = 0;

void usage() {
    printf("extractor.exe -c dev0:dev1:dev2 dnode-dva path\n");
    exit(1);
}

int
main(int argc, char **argv) {
    int c;
    char *path;
    char *vdev = 0;
    char *dnode = 0;
    vector<string> vdevs;
    opterr = 0;

    while ((c = getopt (argc, argv, "vc:d:")) != -1) {
	switch (c)
	{
	case 'v':
	    verbose = 1;
	    break;
	case 'd':
	    break;
	case 'c':
	    vdev = optarg;
	    break;
	default:
	    abort ();
	}
  }

  argc -= optind;
  argv += optind;

  if (!vdev || argc < 2)
      usage();

  char *pch = strtok (vdev,":");
  while (pch != NULL)
  {
      vdevs.push_back(pch);
      pch = strtok (NULL, ":");
  }

  dnode = argv[0];
  path = argv[1];

  Raidz1Device r1(vdevs);

  r1.loadLabel(0,0);

  uint64_t tgx = 2937;
  r1.loadMos(tgx);

  uint64_t dsid;
  r1.loadChildDS(dsid);




  /*
  d = r1.dnode(dnode);
  d->extract(path);
  */

  return 0;
}
