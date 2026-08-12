# Maintainer: Mijael Viricochea <omensight@gmail.com>
#
# Local package: builds fatx360 (mborgerson/fatx + Xbox 360 read/write
# support) directly from this checked-out working tree, uncommitted
# changes included. Run `makepkg -si` from the repository root.

pkgname=fatx360
pkgver=r4e45a5e
pkgrel=1
pkgdesc="FUSE driver for FATX filesystems, with Xbox 360 (big-endian) read/write support"
arch=('x86_64')
url="https://github.com/mborgerson/fatx"
license=('GPL2')
depends=('fuse2')
makedepends=('cmake' 'pkgconf')
provides=('mount.fatxfs')
options=('!strip')

build() {
    local stage="$srcdir/stage"

    cmake -S "$startdir/libfatx" -B "$srcdir/build-libfatx" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$stage"
    cmake --build "$srcdir/build-libfatx"
    cmake --install "$srcdir/build-libfatx"

    cmake -S "$startdir/fatxfs" -B "$srcdir/build-fatxfs" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$stage" \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "$srcdir/build-fatxfs"
}

package() {
    DESTDIR="$pkgdir" cmake --install "$srcdir/build-fatxfs"

    # mount(8) helper: lets `mount -t fatxfs` / fstab entries find the driver,
    # the same way ntfs-3g installs mount.ntfs-3g next to its own binary.
    ln -s fatxfs "$pkgdir/usr/bin/mount.fatxfs"

    install -Dm644 "$startdir/LICENSE.txt" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
