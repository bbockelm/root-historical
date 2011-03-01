#!/bin/bash
#
# Author: Derek Feichtinger, 19 Oct 2005

if test ! -e src/XrdVersion.hh.in; then
   echo "Sanity check. Could not find src/XrdVersion.hh. You need to bootstrap from the xrootd main directory" >&2
   exit 1
fi

# little fix: The build needs this included Makefile to exist for the bootstrap
#             process
if test ! -e src/Makefile_include; then
   touch src/Makefile_include
fi

# create autotools build files from the CVS sources
LIBTOOLIZE=libtoolize

if test -x "`which glibtoolize 2>/dev/null`"; then
    LIBTOOLIZE=glibtoolize
fi

$LIBTOOLIZE --copy --force
aclocal
automake -acf
autoconf

