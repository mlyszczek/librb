# Copyright 1999-2017 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2

EAPI=6
inherit autotools

DESCRIPTION="ring buffer c library with posix-like read/write interface and thread awarness"
HOMEPAGE="http://librb.kurwinet.pl"
SRC_URI="http://distfiles.kurwinet.pl/${PN}/${P}.tar.gz"

LICENSE="BSD-2"
SLOT="0"
KEYWORDS="~amd64 ~x86"
IUSE="threads"

DEPEND=""
RDEPEND="${DEPEND}"

src_configure() {
	econf $(use_enable threads threads)
}

src_install() {
	default
}
