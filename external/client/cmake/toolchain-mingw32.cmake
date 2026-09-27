# DE: Cross-Build fuer 32-Bit-Windows mit MinGW-w64 (posix-Threads).
# EN: Cross build for 32-bit Windows with MinGW-w64 (posix threads).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)
set(CMAKE_C_COMPILER   i686-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER  i686-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# DE: alles statisch -> nur System-DLLs als Abhaengigkeit. EN: all static -> only system DLLs
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
