#!/usr/bin/env bash
# Build the bfin-elf cross compiler Hundekraft's plugin packer needs (GCC 13.3.0, as the 3.x firmware build uses).
# Run inside WSL Ubuntu. Prerequisites (once):
#   sudo apt update && sudo apt install -y build-essential libgmp-dev libmpfr-dev libmpc-dev texinfo flex bison xz-utils curl
# Result: ~/bfin/bin/bfin-elf-gcc, bfin-elf-ld (pass --toolchain or put ~/bfin/bin on PATH for pack.py).
set -euo pipefail

PREFIX="${BFIN_PREFIX:-$HOME/bfin}"
WORK="${BFIN_WORK:-$HOME/bfin-build}"
BINUTILS=binutils-2.42
GCC=gcc-13.3.0
JOBS="$(nproc)"

mkdir -p "$PREFIX" "$WORK"
cd "$WORK"
[ -f $BINUTILS.tar.xz ] || curl -fL -o $BINUTILS.tar.xz https://ftp.gnu.org/gnu/binutils/$BINUTILS.tar.xz
[ -f $GCC.tar.xz ] || curl -fL -o $GCC.tar.xz https://ftp.gnu.org/gnu/gcc/$GCC/$GCC.tar.xz
sha256sum $BINUTILS.tar.xz $GCC.tar.xz

if [ ! -x "$PREFIX/bin/bfin-elf-ld" ]; then
  rm -rf $BINUTILS build-binutils && tar xf $BINUTILS.tar.xz && mkdir build-binutils && cd build-binutils
  ../$BINUTILS/configure --target=bfin-elf --prefix="$PREFIX" --disable-nls --disable-werror --disable-gdb --disable-sim
  make -j"$JOBS" && make install
  cd "$WORK"
fi
export PATH="$PREFIX/bin:$PATH"

if [ ! -x "$PREFIX/bin/bfin-elf-gcc" ]; then
  rm -rf $GCC build-gcc && tar xf $GCC.tar.xz && mkdir build-gcc && cd build-gcc
  ../$GCC/configure --target=bfin-elf --prefix="$PREFIX" --enable-languages=c --without-headers --with-newlib \
    --disable-nls --disable-shared --disable-threads --disable-libssp --disable-libquadmath --disable-libgomp
  make -j"$JOBS" all-gcc all-target-libgcc && make install-gcc install-target-libgcc
  cd "$WORK"
fi

bfin-elf-gcc --version | head -1
bfin-elf-gcc -mcpu=bf524 -print-libgcc-file-name
