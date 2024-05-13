#!/bin/bash

if [ -z $ANDROID_HOME ]; then
	echo "ANDROID_HOME is not defined. Set this to \$HOME/Android for example."
	exit 1
fi

echo "=== Using \$ANDROID_HOME = $ANDROID_HOME ==="

if [ -d $ANDROID_HOME ]; then
	echo "$ANDROID_HOME is already populated. This script will install from scratch."
	exit 1
fi

# Grab from https://developer.android.com/studio
# Adjust as necessary. Not sure if there's a generic way to do this ...
echo "=== Downloading command line tools for Linux ==="
VERSION=linux-11076708
FILENAME=commandlinetools-${VERSION}_latest.zip
wget https://dl.google.com/android/repository/$FILENAME || exit 1

mkdir "$ANDROID_HOME"
mv "$FILENAME" "$ANDROID_HOME/"
cd "$ANDROID_HOME"
echo "=== Unzipping command line tools ==="
unzip "$FILENAME" >/dev/null 2>&1 || exit 1
rm "$FILENAME"
mkdir cmdline-tools/tools
mv cmdline-tools/{NOTICE.txt,bin,lib,source.properties} cmdline-tools/tools

export PATH="$PATH:$ANDROID_HOME/cmdline-tools/tools/bin:$ANDROID_HOME/platform-tools"

echo "=== Make sure JDK 17 is enabled (e.g. for Arch) ==="
echo "  pacman -S jre17-openjdk"
echo "  archlinux-java set java-17-openjdk"

echo "=== Automatically accepting all relevant licenses ==="
yes | sdkmanager --licenses >/dev/null 2>&1 || exit 1

echo "=== Done! Now update PATH in shell ==="
echo "  export PATH=\"\$PATH:\$ANDROID_HOME/cmdline-tools/tools/bin:\$ANDROID_HOME/platform-tools\""
