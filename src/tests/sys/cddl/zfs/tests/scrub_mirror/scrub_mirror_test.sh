# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

# $FreeBSD: releng/12.0/tests/sys/cddl/zfs/tests/scrub_mirror/scrub_mirror_test.sh 329867 2018-02-23 16:31:00Z asomers $

#
# Copyright 2012 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#


atf_test_case scrub_mirror_001_pos cleanup
scrub_mirror_001_pos_head()
{
	atf_set "descr" "The primary side of a zpool mirror may be completely wipedwithout affecting the content of the pool"
	atf_set "require.progs"  zfs
}
scrub_mirror_001_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/scrub_mirror_common.kshlib
	. $(atf_get_srcdir)/default.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/scrub_mirror_001_pos.ksh || atf_fail "Testcase failed"
}
scrub_mirror_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/scrub_mirror_common.kshlib
	. $(atf_get_srcdir)/default.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case scrub_mirror_002_pos cleanup
scrub_mirror_002_pos_head()
{
	atf_set "descr" "The primary side of a zpool mirror may be completely wipedwithout affecting the content of the pool"
	atf_set "require.progs"  zfs
}
scrub_mirror_002_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/scrub_mirror_common.kshlib
	. $(atf_get_srcdir)/default.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/scrub_mirror_002_pos.ksh || atf_fail "Testcase failed"
}
scrub_mirror_002_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/scrub_mirror_common.kshlib
	. $(atf_get_srcdir)/default.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case scrub_mirror_003_pos cleanup
scrub_mirror_003_pos_head()
{
	atf_set "descr" "The primary side of a zpool mirror may be completely mangledwithout affecting the content of the pool"
	atf_set "require.progs"  zfs
}
scrub_mirror_003_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/scrub_mirror_common.kshlib
	. $(atf_get_srcdir)/default.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/scrub_mirror_003_pos.ksh || atf_fail "Testcase failed"
}
scrub_mirror_003_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/scrub_mirror_common.kshlib
	. $(atf_get_srcdir)/default.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_test_case scrub_mirror_004_pos cleanup
scrub_mirror_004_pos_head()
{
	atf_set "descr" "The primary side of a zpool mirror may be completely mangledwithout affecting the content of the pool"
	atf_set "require.progs"  zfs
}
scrub_mirror_004_pos_body()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/scrub_mirror_common.kshlib
	. $(atf_get_srcdir)/default.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/scrub_mirror_004_pos.ksh || atf_fail "Testcase failed"
}
scrub_mirror_004_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/scrub_mirror_common.kshlib
	. $(atf_get_srcdir)/default.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case scrub_mirror_001_pos
	atf_add_test_case scrub_mirror_002_pos
	atf_add_test_case scrub_mirror_003_pos
	atf_add_test_case scrub_mirror_004_pos
}