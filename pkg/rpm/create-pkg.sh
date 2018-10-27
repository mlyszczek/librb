#!/bin/sh

project="librb"
scp_server="pkgs@kurwik"

if [ ${#} -ne 3 ]
then
    echo "usage: ${0} <version> <arch> <host_os>"
    echo ""
    echo "where"
    echo "    <version>        git tag version"
    echo "    <arch>           target architecture"
    echo "    <host_os>        target os (rhel-7, centos-7 etc)"
    exit 1
fi

git_version="${1}"
arch="${2}"
host_os="${3}"

cd "${HOME}/rpmbuild"
pkg_version="$(curl "https://git.kurwinet.pl/${project}/plain/configure.ac?h=${git_version}" | \
    grep "AC_INIT(" | cut -f3 -d\[ | cut -f1 -d\])"
wget "https://git.kurwinet.pl/${project}/snapshot/${project}-${git_version}.tar.gz" \
    -O "SOURCES/${project}-${pkg_version}.tar.gz"
wget "https://git.kurwinet.pl/${project}/plain/pkg/rpm/librb.spec.template?h=${git_version}" \
    -O "SPECS/librb-${pkg_version}.spec"
lt_version="$(curl "https://git.kurwinet.pl/${project}/plain/Makefile.am?h=${git_version}" | \
    grep "librb_la_LDFLAGS = -version-info" | cut -f4 -d\ )"

current="$(echo ${lt_version} | cut -f1 -d:)"
revision="$(echo ${lt_version} | cut -f2 -d:)"
age="$(echo ${lt_version} | cut -f3 -d:)"

lib_version="$(( current - age )).${age}.${revision}"
abi_version="$(( current - age ))"
rel_version="$(cat SPECS/${project}-${pkg_version}.spec | \
    grep "Release:" | awk '{print $2}')"

sed -i "s/@{VERSION}/${pkg_version}/" SPECS/${project}-${pkg_version}.spec
sed -i "s/@{GIT_VERSION}/${git_version}/" SPECS/${project}-${pkg_version}.spec
sed -i "s/@{LIB_VERSION}/${lib_version}/" SPECS/${project}-${pkg_version}.spec
sed -i "s/@{ABI_VERSION}/${abi_version}/" SPECS/${project}-${pkg_version}.spec

rpmbuild -ba SPECS/${project}-${pkg_version}.spec || exit 1

###
# verify
#

yum -y install "RPMS/${arch}/${project}-${pkg_version}-${rel_version}.${arch}.rpm" \
    "RPMS/${arch}/${project}-devel-${pkg_version}-${rel_version}.${arch}.rpm"

failure=0
gcc "BUILD/${project}-${git_version}/pkg/test.c" -lrb -o /tmp/librb-test || failure=1
/tmp/librb-test || failure=1

yum -y remove "${project}" "${project}-devel"

if [ ${failure} -eq 1 ]
then
    exit 1
fi

if [ -n "${scp_server}" ]
then
    echo "copying data to ${scp_server}:${project}/${host_os}/${arch}"
    scp "RPMS/${arch}/${project}-${pkg_version}-${rel_version}.${arch}.rpm" \
        "RPMS/${arch}/${project}-devel-${pkg_version}-${rel_version}.${arch}.rpm" \
        "RPMS/${arch}/${project}-debuginfo-${pkg_version}-${rel_version}.${arch}.rpm" \
        "${scp_server}:${project}/${host_os}/${arch}" || exit 1
fi
