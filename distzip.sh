#!/bin/sh

REVISION=`git rev-parse --short HEAD`
CEFVERSION=`grep "^CEF Version: " subprojects/cef/README.txt | awk '{ print $3 }'`
ARCH=$2

# prepare directory
rm -Rf dist-release
mkdir -p dist-release

# jump into Release Folder and start zipping
cd $1/Release

# executable and static content
tar -cvzf ../../dist-release/$2-dist-cefbrowser-exe-${REVISION}.tar.gz cefbrowser js css database application fonts

# cef
tar --exclude "cefbrowser" --exclude "vdrclient" --exclude "js" --exclude "css" --exclude "database" --exclude "application" --exclude "fonts" -cvzf ../../dist-release/$2-dist-cefbrowser-cef-${CEFVERSION}.tar.gz js *

echo "CEFVERSION=${CEFVERSION}" >> ../../dist-release/$2-current-version.txt
echo "REVISION=${REVISION}" >> ../../dist-release/$2-current-version.txt