#!/bin/bash
set -x

pushd $(dirname "$0") > /dev/null
SCRIPTDIR=$(pwd -P)
popd > /dev/null

export SRCROOT="$SCRIPTDIR/.."
export DSTROOT="$SCRIPTDIR"
export DERIVED_FILES_DIR="$SCRIPTDIR"
export SDKROOT="$SRCROOT/../../../Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"

# copied and modified from dyld.xcodeproj
${SRCROOT}/build-scripts/libdyld-generate-version-headers.sh