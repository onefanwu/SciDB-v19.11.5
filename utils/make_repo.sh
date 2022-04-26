#!/bin/bash
#
# BEGIN_COPYRIGHT
#
# Copyright (C) 2008-2019 SciDB, Inc.
# All Rights Reserved.
#
# SciDB is free software: you can redistribute it and/or modify
# it under the terms of the AFFERO GNU General Public License as published by
# the Free Software Foundation.
#
# SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
# INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
# NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
# the AFFERO GNU General Public License for the complete license terms.
#
# You should have received a copy of the AFFERO GNU General Public License
# along with SciDB.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
#
# END_COPYRIGHT
#

#
# Script for creating simple APT/YOUM deb repo in current directory
#

function usage
{
    echo "Usage: $0 <apt|yum> <package_directory> [<gpg key id>]"
    exit 1
}

[ $# -lt 2 ] && usage

repotype="${1,,}"
package_dir="${2}"
key="${3}"
#
# Check directory if it has files for building repo
# Params:
#   $1 - directory for checking
# Return codes:
#   0 - all ok
#   exit 1 - error
#
function check_apt_dir
{
    if [ "`find -H "$1" -maxdepth 1 -type f -iname \*.deb | wc -l`" = 0 ]; then
        echo "ERROR: Can not find .deb files in '$1'. Can not create repository here."
        exit 1
    fi
    return 0
}

function check_yum_dir
{
    if [ "`find -H "$1" -maxdepth 1 -type f -iname \*.rpm | wc -l`" = 0 ]; then
        echo "ERROR: Can not find .rpm files in '$1'. Can not create repository here."
        exit 1
    fi
    return 0
}

function die
{
    echo "ERROR: Something went wrong in '$1'! Aborting process!"
    exit 1
}

function expect_rpmsign_script
{
cat << ENDOFSCRIPT
spawn rpm --define "_gpg_name $2" --addsign "$1"
expect -exact "Enter pass phrase: "
send -- "Secret passphrase\r"
expect eof
catch wait result
exit [lindex \$result 3]
ENDOFSCRIPT
}

if [ "$repotype" == "apt" ]; then
    echo Checking dir $package_dir
    check_apt_dir "$package_dir"
    if [ "$?" = "0" ]; then
        echo Building repo in "$package_dir"
        echo Cleanup old repo files
        rm -f "$package_dir"/{Packages*,Sources*,*Release*,Contents*}
        echo Scanning files
        dpkg-scanpackages "$package_dir" > "$package_dir"/Packages || die dpkg-scanpackages
        dpkg-scansources "$package_dir" > "$package_dir"/Sources || die dpkg-scansources
        apt-ftparchive contents "$package_dir" > "$package_dir"/Contents || die apt-ftparchive
        echo Codename: `echo $package_dir|sed 's/\/$//'`> "$package_dir"/Release
        apt-ftparchive release "$package_dir" >> "$package_dir"/Release || die apt-ftparchive
	if [ ! -z "${key}" ]; then
            echo Signing repo
            gpg -u "${key}" -abs -o "$package_dir"/Release.gpg "$package_dir"/Release || die gpg
            gpg -u "${key}" --clearsign -o "$package_dir"/InRelease "$package_dir"/Release || die gpg
	fi
    fi
elif [ "$repotype" == "yum" ]; then
    echo Checking dir $package_dir
    check_yum_dir "$package_dir"
    if [ "$?" = "0" ]; then
	if [ ! -z "${key}" ]; then
            for f in $package_dir/*.rpm; do
		echo Signing package $f
		expect_rpmsign_script "$f" "${key}" | /usr/bin/expect - || die expect_rpmsign_script
            done
	fi
        echo Building repo inside $package_dir
        rm -rf "$package_dir/repodata"
        createrepo "$package_dir"
	if [ ! -z "${key}" ]; then
            echo Signing repo $package_dir
            gpg -u "${key}" --detach-sign --armor "$package_dir/repodata/repomd.xml"
	fi
    fi
else
    echo Unknown repo type \'$repotype\'
    usage
fi

echo Done! Have a nice day!
