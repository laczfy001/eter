#!/bin/sh
# A fordító és a linker verziója egy sorban (a statikus FFmpeg ehhez illeszkedik, lásd build_windows.sh).
echo "gcc $(${CC:-gcc} -dumpfullversion 2>/dev/null || ${CC:-gcc} -dumpversion) / $(ld --version 2>/dev/null | head -n 1)"
