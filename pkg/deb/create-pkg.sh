#!/bin/sh

scp_server="pkgs@kurwik"
project="librb"

if [ ${#} -ne 3 ]
then
    echo "usage: ${0} <version> <arch> <host_os>"
    echo ""
    echo "where:"
    echo "    <version>         git tag version to build (without prefix v)"
    echo "    <arch>            target architecture"
    echo "    <host_os>         target os (debian9, debian8 etc)"
    echo ""
    echo "example"
    echo "      ${0} 1.0.0 i386 debian9"
    exit 1
fi

version="${1}"
arch="${2}"
host_os="${3}"

###
# preparing
#

rm -rf "/tmp/${project}-${version}"
mkdir "/tmp/${project}-${version}"

cd "/tmp/${project}-${version}"
git clone "https://git.bofc.pl/${project}"
cd "${project}"

git checkout "${version}" || exit 1

if [ ! -d "pkg/deb" ]
then
    echo "pkg/deb does not exist, cannot create debian pkg"
    exit 1
fi

version="$(grep "AC_INIT(" "configure.ac" | cut -f3 -d\[ | cut -f1 -d\])"
abi_version="$(echo ${version} | cut -f1 -d.)"

echo "version ${version}"
echo "abi version ${abi_version}"

###
# building package
#

codename="$(lsb_release -c | awk '{print $2}')"

cp -r "pkg/deb/" "debian"
sed -i "s/\${DATE}/$(date -R)/" "debian/changelog.template"
sed -i "s/\${VERSION}/${version}/" "debian/changelog.template"
sed -i "s/\${CODENAME}/${codename}/" "debian/changelog.template"
sed -i "s/\${ABI_VERSION}/${abi_version}/" "debian/control.template"

mv "debian/changelog.template" "debian/changelog"
mv "debian/control.template" "debian/control"

debuild -us -uc || exit 1

###
# verifying
#

cd ..

# debuild doesn't fail when lintial finds an error, so we need
# to check it manually, it doesn't take much time, so whatever

for d in *.deb
do
    echo "Running lintian on ${d}"
    lintian ${d} || exit 1
done

dpkg -i "${project}${abi_version}_${version}_${arch}.deb" || exit 1
dpkg -i "${project}-dev_${version}_${arch}.deb" || exit 1

failed=0
gcc ${project}/pkg/test.c -o testprog -lrb || failed=1

if ldd ./testprog | grep "\/usr\/bofc"
then
    # sanity check to make sure test program uses system libraries
    # and not locally installed ones (which are used as build
    # dependencies for other programs

    echo "test prog uses libs from manually installed /usr/bofc \
        instead of system path!"
    failed=1
fi

./testprog || failed=1

dpkg -r "${project}${abi_version}" "${project}-dev" || exit 1

if [ ${failed} -eq 1 ]
then
    exit 1
fi

if [ -n "${scp_server}" ]
then
    dbgsym_pkg="${project}${abi_version}-dbgsym_${version}_${arch}.deb"

    if [ ! -f "${dbgsym_pkg}" ]
    then
        # on some systems packages with debug symbols are created with
        # ddeb extension and not deb
        dbgsym_pkg="${project}${abi_version}-dbgsym_${version}_${arch}.ddeb"
    fi

    echo "copying data to ${scp_server}:${project}/${host_os}/${arch}"
    scp "${project}-dev_${version}_${arch}.deb" \
        "${dbgsym_pkg}" \
        "${project}${abi_version}_${version}_${arch}.deb" \
        "${project}_${version}.dsc" \
        "${project}_${version}.tar.xz" \
        "${project}_${version}_${arch}.build" \
        "${project}_${version}_${arch}.buildinfo" \
        "${project}_${version}_${arch}.changes" \
        "${scp_server}:${project}/${host_os}/${arch}" || exit 1
fi
