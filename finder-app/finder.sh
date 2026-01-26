#!/bin/sh

if [ $# -ne 2 ]; then
	echo "ERROR: Invalid number of arguments ($?), expecting 2"
	exit 1
fi

if [ ! -d $1 ]; then
	echo "$1 is not a valid file directory"
	exit 1
fi

filesdir=$1
searchstr=$2

file_count=$(grep -r -l "${searchstr}" "${filesdir}" | wc -l)
line_count=$(grep -r -o "${searchstr}" "${filesdir}" | wc -l)

echo "The number of files are ${file_count} and the number of matching lines are ${line_count}"
