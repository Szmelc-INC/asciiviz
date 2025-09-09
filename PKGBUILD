pkgname=asciiviz-git
_pkgname=asciiviz
pkgver=0
pkgrel=1
pkgdesc="ASCII/ANSI function visualizer with fractal presets and palette support"
arch=('x86_64')
url="https://github.com/Szmelc-INC/asciiviz"
license=('custom')
depends=('glibc')
makedepends=('git' 'make' 'gcc')
provides=('asciiviz')
conflicts=('asciiviz')
source=("$_pkgname::git+https://github.com/Szmelc-INC/asciiviz.git")
sha256sums=('SKIP')

pkgver() {
  cd "${srcdir}/${_pkgname}"
  printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
  cd "${srcdir}/${_pkgname}"
  make
}

package() {
  cd "${srcdir}/${_pkgname}"
  install -Dm755 asciiviz "${pkgdir}/usr/bin/asciiviz"
  install -Dm644 README.md "${pkgdir}/usr/share/doc/${_pkgname}/README.md"
  install -Dm644 functions/*.cfg "${pkgdir}/usr/share/${_pkgname}/functions/"
  install -Dm644 palettes/char/*.cfg "${pkgdir}/usr/share/${_pkgname}/palettes/char/"
  install -Dm644 palettes/col/*.cfg "${pkgdir}/usr/share/${_pkgname}/palettes/col/"
}
