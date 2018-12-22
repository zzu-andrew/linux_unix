#---
# Excerpted from "Test-Driven Development for Embedded C",
# published by The Pragmatic Bookshelf.
# Copyrights apply to this code. It may not be used to create training material, 
# courses, books, articles, and the like. Contact us if you are in doubt.
# We make no guarantees that this code is fit for any purpose. 
# Visit http://www.pragmaticprogrammer.com/titles/jgade for more book information.
#---
#/bin/bash -x
CPP_U_TEST=$(pwd)/..

EXE_DIR=${EXE_DIR:-/usr/local/bin}
test -f ${EXE_DIR} || mkdir -p ${EXE_DIR}

NEW_SCRIPTS="NewCIoDriver NewClass NewInterface NewCModule NewCmiModule NewProject NewLibrary NewPackageDirs NewCInterface NewCFunction NewHelp"


for file in $NEW_SCRIPTS ; do
   rm -f ${EXE_DIR}/${file}
   ln -s ${CPP_U_TEST}/scripts/${file}.sh ${EXE_DIR}/${file}
   chmod a+x ${EXE_DIR}/${file}
done
