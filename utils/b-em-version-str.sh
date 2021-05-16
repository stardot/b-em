#!/bin/sh
#
# b-em-version-str: emits the version of b-em which is building.
#		    If this is a release build, then the tag name is chomped
#		    to remove extraneous git information.
#
#		    If it's a developer build, it's left as-is.
#
#
#
# Intended to be called from configure.ac (via autogen.sh)

if [ -d .git ]
then
    if grep -q -i '^ISRELEASED="yes"' configure.ac; then
		# A release build.  Strip the git information off the tag name.
		git describe --tags --abbrev=0 2>/dev/null || echo unknown
	else
		git describe --always --exclude m5000pi
	fi
else
    if [ -d ../.git ]
    then
		if grep -q -i '^ISRELEASED="yes"' ../configure.ac; then
			# A release build.  Strip the git information off the tag name.
			ver=`git describe --tags --abbrev=0 2>/dev/null || echo unknown`
		else
			ver=`git describe --always --exclude m5000pi`
		fi
		echo "#define REAL_VERSION_STR \"B-Em $ver\"" > version.h
		echo "EQUS \"$ver\":EQUB 0" > version.asm
	fi
fi
