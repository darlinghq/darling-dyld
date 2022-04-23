#!/bin/bash
set -x

export SRCROOT="$(cd ../.. && pwd)"
DARLING_ROOT_DIR="$(cd $SRCROOT/../../.. && pwd)"

# SRCROOT
export DSTROOT="$SRCROOT/gen"
export DERIVED_FILES_DIR="$SRCROOT/gen/tmp"
export SDKROOT="$DARLING_ROOT_DIR/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"

${SRCROOT}/build-scripts/libdyld-generate-version-headers.sh
ln -s -f ../../gen/usr/local/include/mach-o/dyld_priv.h --target-directory "$SRCROOT/include/mach-o"