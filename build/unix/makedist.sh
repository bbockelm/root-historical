#! /bin/sh

# Script to produce binary distribution of ROOT.
# Called by main Makefile.
#
# Author: Fons Rademakers, 29/2/2000

ROOTVERS=`cat build/version_number | sed -e 's/\//\./'`
TYPE=`bin/root-config --arch`
if [ "x`bin/root-config --platform`" = "xmacosx" ]; then
   TYPE=$TYPE-`sw_vers -productVersion | cut -d . -f1 -f2`
   TYPE=$TYPE-`uname -p`
fi
if [ "x`bin/root-config --platform`" = "xsolaris" ]; then
   TYPE=$TYPE-`uname -r`
   TYPE=$TYPE-`uname -p`
fi

# debug build?
DEBUG=`grep ROOTBUILD config/Makefile.config | sed 's,^ROOTBUILD.*= \([^[:space:]]*\)$,\1,'`
if [ "x${DEBUG}" != "x" ]; then
   DEBUG=".debug"
fi

# MSI?
if [ "x$1" = "x-msi" ]; then
   MSI=1
   shift
fi

# compiler specified?
COMPILER=$1
if [ "x${COMPILER}" != "x" ]; then
   COMPILER="-${COMPILER}"
fi

TARFILE=root_v${ROOTVERS}.${TYPE}${COMPILER}${DEBUG}
# figure out which tar to use
if [ "x$MSI" = "x1" ]; then
   TARFILE=../${TARFILE}.msi
   TARCMD="build/package/msi/makemsi.sh ${TARFILE} -T ${TARFILE}.filelist"
else
   TARFILE=${TARFILE}.tar
   ISGNUTAR="`tar --version 2>&1 | grep GNU`"
   if [ "x${ISGNUTAR}" != "x" ]; then
      TAR=tar
   else
      if [ "x`which gtar 2>/dev/null | awk '{if ($1~/gtar/) print $1;}'`" != "x" ]; then
	 TAR=gtar
      fi
   fi
   if [ "x${TAR}" != "x" ]; then
      TARFILE=${TARFILE}".gz"
      TARCMD="${TAR} zcvf ${TARFILE} -T ${TARFILE}.filelist"
   else
      TARCMD="tar cvf ${TARFILE}"
      DOGZIP="y"
   fi
fi

cp -f main/src/rmain.cxx include/
pwd=`pwd`
if [ "x${MSI}" = "x" ]; then
   dir=`basename $pwd`
   cd ..
fi

${pwd}/build/unix/distfilelist.sh $dir > ${TARFILE}.filelist
rm -f ${TARFILE}
if [ "x${TAR}" != "x" ] || [ "x$MSI" = "x1" ]; then
   $TARCMD || exit 1
else
   $TARCMD `cat ${TARFILE}.filelist` || exit 1
fi
rm ${TARFILE}.filelist 

if [ "x$DOGZIP" = "xy" ]; then
   rm -f ${TARFILE}.gz
   gzip $TARFILE
fi

cd $pwd
rm -f include/rmain.cxx

exit 0
