#define __EXTENSIONS__
#define _REENTRANT
#define _POSIX_PTHREAD_SEMANTICS
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE 1
#define HAVE_LARGE_STACKS 1


#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <ctype.h>
#include <vector>
#include <map>
#include <string>
#include <unistd.h>
#include <assert.h>

#include "defs.h"
#include "zio.h"

using namespace std;
