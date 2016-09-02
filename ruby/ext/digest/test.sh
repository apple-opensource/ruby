#!/bin/sh
#
# $RoughId: test.sh,v 1.5 2001/07/13 15:38:27 knu Exp $
# $Id: test.sh,v 1.1.1.2 2003/10/15 10:11:47 melville Exp $

RUBY=${RUBY:=ruby}
MAKE=${MAKE:=make}
CFLAGS=${CFLAGS:=-Wall}

${RUBY} extconf.rb --with-cflags="${CFLAGS}"
${MAKE} clean
${MAKE}

mkdir -p lib/digest

for algo in md5 rmd160 sha1 sha2; do
    args=--with-cflags="${CFLAGS}"

    if [ $WITH_BUNDLED_ENGINES ]; then
	args="$args --with-bundled-$algo"
    fi

    (cd $algo &&
	${RUBY} extconf.rb $args;
	${MAKE} clean;
	${MAKE})
    ln -sf ../../$algo/$algo.so lib/digest/
done

${RUBY} -I. -I./lib test.rb

rm lib/digest/*.so
rmdir lib/digest
