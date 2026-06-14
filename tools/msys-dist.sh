#!/bin/bash

if [[ ! -x melonPrimeDS.exe ]]; then
	echo "Run this script from the directory you built melonPrimeDS."
	exit 1
fi

mkdir -p dist

for lib in $(ldd melonPrimeDS.exe | grep mingw | sed "s/.*=> //" | sed "s/(.*)//"); do
	cp "${lib}" dist
done

cp melonPrimeDS.exe dist
windeployqt dist
