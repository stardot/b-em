# Arch PKGBUILD for B-Em (Allego 5 GIT Version)
# Maintainer: Steve Fosdick <steve@fosdick.me.uk>

pkgname=b-em-a5-git
pkgver=VERSION
pkgrel=1
pkgdesc="An Emulator for the BBC Microcomputer system and Tube Processors"
arch=('x86_64' 'i686')
url="https://github.com/stardot/b-em.git"
license=('GPL')
depends=('allegro')
makedepends=('git')
provides=("${pkgname%-git}")
conflicts=("${pkgname%-git}")
replaces=()
backup=()
options=()
source=("${pkgname%-git}::git+https://github.com/stardot/b-em.git#branch=sf/allegro5")
md5sums=('SKIP')

pkgver() {
    cd "$srcdir/${pkgname%-git}"
    printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
    cd "$srcdir/${pkgname%-git}"
    ./autogen.sh
    ./configure --prefix=/usr
    make
}

package() {
    cd "$srcdir/${pkgname%-git}"
    make DESTDIR="$pkgdir/" install
}
