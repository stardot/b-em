#!/bin/sh
#
# b-em-version-str: emits the version of b-em which is building.
#                   If this is a release build, then the tag name is chomped
#                   to remove extraneous git information.
#
#                   If it's a developer build, it's left as-is.
#
#
#
# Intended to be called from configure.ac (via autogen.sh)
B_EM_VERSION=2.2

[ -d ".git" ] || { echo "$B_EM_VERSION" ; exit ; }

if grep -q -i '^ISRELEASED="yes"' ./configure.ac; then
    # A release build.  Strip the git information off the tag name.
    git describe --tags --abbrev=0 2>/dev/null || echo "$B_EM_VERSION"
else
    git describe --always --exclude m5000pi
fi
