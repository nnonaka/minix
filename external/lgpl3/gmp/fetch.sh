#!/bin/sh

# Make sure we're in our directory (i.e., where this shell script is)
echo $0
cd `dirname $0`

# Configure fetch method
URL="https://gmplib.org/download/gmp/gmp-6.1.2.tar.bz2"
FETCH=https
which curl >/dev/null
if [ $? -eq 0 ]; then
	FETCH="curl -O -f"
fi

# Fetch sources if not available
if [ ! -d dist ];
then
        if [ ! -f gmp-6.1.2.tar.bz2 ]; then
		$FETCH $URL
		if [ $? -ne 0 ]; then
			$FETCH $BACKUP_URL
		fi
	fi

	tar -oxjf gmp-6.1.2.tar.bz2
	mv gmp-6.1.2 dist && \
	cd dist && \
	cat ../patches/* |patch -p1
fi

