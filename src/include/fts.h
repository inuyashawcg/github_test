/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)fts.h	8.3 (Berkeley) 8/14/94
 * $FreeBSD: releng/12.0/include/fts.h 326024 2017-11-20 19:45:28Z pfg $
 */

#ifndef	_FTS_H_
#define	_FTS_H_

#include <sys/_types.h>

/*
	Two structures are defined (and typedef'd) in the include file <fts.h>.
	The first is FTS, the structure that represents the file hierarchy itself.
	The	second is FTSENT, the structure	that represents	a file in the file
	hierarchy. Normally, an FTSENT structure is returned for every file in
	the file hierarchy. 
*/
typedef struct {
	struct _ftsent *fts_cur;	/* current node */
	struct _ftsent *fts_child;	/* linked list of children */
	struct _ftsent **fts_array;	/* sort array */
	__dev_t fts_dev;		/* starting device # */
	char *fts_path;			/* path for this descent */
	int fts_rfd;			/* fd for root */
	__size_t fts_pathlen;		/* sizeof(path) */
	__size_t fts_nitems;		/* elements in the sort array */
	int (*fts_compar)		/* compare function */
	    (const struct _ftsent * const *, const struct _ftsent * const *);

/*
	This option causes any symbolic link specified as a root path
	to be followed immediately whether or not FTS_LOGICAL is also
	specified.
*/
#define	FTS_COMFOLLOW	0x001		/* follow command line symlinks */
/*
	This option causes the fts routines to return FTSENT structures
	for the targets of symbolic links instead of the symbolic links
	themselves. If this option is set, the only symbolic links for
	which FTSENT structures are returned to the application are those
	referencing non-existent files. Either FTS_LOGICAL or FTS_PHYSICAL
	must be provided to the fts_open() function.
*/
#define	FTS_LOGICAL	0x002		/* logical walk */
/*
	To allow descending to arbitrary depths (independent of {PATH_MAX})
	and improve performance, the fts functions change directories as
	they walk the file hierarchy.	This has the side-effect that an
	application cannot rely on being in any particular directory during
	the traversal. The FTS_NOCHDIR option turns off	this feature, and
	the fts functions will not change the current directory. Note that
	applications should not themselves change their current directory
	and try to access files unless FTS_NOCHDIR is specified and absolute
	pathnames	were provided as arguments to fts_open().
*/
#define	FTS_NOCHDIR	0x004		/* don't change directories */
/*
	By default, returned FTSENT structures reference file characteristic
	information (the statp field) for	each file visited. This option relaxes
	that requirement as a performance optimization, allowing the fts functions
	to set the fts_info field to FTS_NSOK and leave	the contents of	the statp
	field undefined.
*/
#define	FTS_NOSTAT	0x008		/* don't get stat info */
/*
	This option causes the fts routines to return FTSENT structures for
	symbolic links themselves instead of the target files they point to.
	If this option is set, FTSENT structures for all symbolic links	in
	the hierarchy are returned to the application. Either FTS_LOGICAL or
	FTS_PHYSICAL must	be provided to the fts_open() function.
*/
#define	FTS_PHYSICAL	0x010		/* physical walk */
/*
	By default, unless they are specified as path arguments to fts_open(),
	any files named `.' or `..' encountered in the file	hierarchy are ignored.
	This option causes the fts routines to return FTSENT structures	for them.
*/
#define	FTS_SEEDOT	0x020		/* return dot and dot-dot */
/*
	This option prevents fts from descending into directories that have a
	different device number than the file from which the descent began.
*/
#define	FTS_XDEV	0x040		/* don't cross devices */
#define	FTS_WHITEOUT	0x080		/* return whiteout information */
#define	FTS_OPTIONMASK	0x0ff		/* valid user option mask */

#define	FTS_NAMEONLY	0x100		/* (private) child names only */
#define	FTS_STOP	0x200		/* (private) unrecoverable error */
	int fts_options;		/* fts_open options, global flags */
	void *fts_clientptr;		/* thunk for sort function */
} FTS;

typedef struct _ftsent {
	/*
		If a directory causes	a cycle	in the hierarchy (see FTS_DC),
		either because of a hard link	between	two directories, or a
		symbolic link	pointing to a directory, the fts_cycle field
		of the structure will	point to the FTSENT structure in the
		hierarchy that references the	same file as the current
		FTSENT structure. Otherwise, the contents of	the fts_cycle
		field	are undefined.
	*/
	struct _ftsent *fts_cycle;	/* cycle node */
	/*
		A pointer to the FTSENT structure referencing	the file in
		the hierarchy	immediately above the current file, i.e., the
		directory of which this file is a member. A parent structure
		for the initial entry point is provided as well, however, 
		only the fts_level, fts_number and fts_pointer fields are
		guaranteed to be initialized.
	*/
	struct _ftsent *fts_parent;	/* parent directory */
	/*
		Upon return from the fts_children() function,	the fts_link
		field	points to the next structure in	the NULL-terminated
		linked list of directory members. Otherwise, the contents
		of the fts_link field	are undefined.
	*/
	struct _ftsent *fts_link;	/* next file in directory */
	/*
		This field is	provided for the use of	the application	program
		and is not modified by the fts functions. It is initialized to 0.
	*/
	long long fts_number;		/* local numeric value */
#define	fts_bignum	fts_number	/* XXX non-std, should go away */
	/*
		This field is	provided for the use of	the application	program
		and is not modified by the fts functions. It is initialized to NULL.
	*/
	void *fts_pointer;		/* local address value */
	/*
		A path for accessing the file	from the current directory.
	*/
	char *fts_accpath;		/* access path */
	/*
		The path for the file	relative to the	root of	the traversal.
		This path contains the path specified	to fts_open() as a prefix(前缀).
	*/
	char *fts_path;			/* root path */
	/*
		Upon return of a FTSENT structure from the fts_children() or
		fts_read() functions,	with its fts_info field	set to FTS_DNR,
		FTS_ERR or FTS_NS, the fts_errno field contains the value	of
		the external variable errno specifying the cause of the error.
		Otherwise, the contents of the fts_errno field are undefined.
	*/
	int fts_errno;			/* errno for this node */
	int fts_symfd;			/* fd for symlink */
	__size_t fts_pathlen;		/* strlen(fts_path) */
	__size_t fts_namelen;		/* strlen(fts_name) */

	__ino_t fts_ino;		/* inode */
	__dev_t fts_dev;		/* device */
	__nlink_t fts_nlink;		/* link count */

#define	FTS_ROOTPARENTLEVEL	-1
#define	FTS_ROOTLEVEL		 0
	/*
		The depth of the traversal, numbered from -1 to N, where
		this file was	found.	The FTSENT structure representing the
		parent of the	starting point (or root) of the	traversal is
		numbered FTS_ROOTPARENTLEVEL (-1), and the FTSENT structure
		for the root itself is numbered FTS_ROOTLEVEL	(0).
	*/
	long fts_level;			/* depth (-1 to N) */

/*
	One of the following values describing the returned FTSENT
	structure and	the file it represents.	 With the exception of
	directories without errors (FTS_D), all of these entries are
	terminal, that is, they will not be revisited, nor will any
	of their descendants be visited.
*/
#define	FTS_D		 1		/* preorder directory - 前序目录 */
#define	FTS_DC		 2		/* directory that causes cycles */
#define	FTS_DEFAULT	 3		/* none of the above */
#define	FTS_DNR		 4		/* unreadable directory - 无法达到的目录 */
#define	FTS_DOT		 5		/* dot or dot-dot */
#define	FTS_DP		 6		/* postorder directory - 后续目录 */
#define	FTS_ERR		 7		/* error; errno is set */
#define	FTS_F		 8		/* regular file */
#define	FTS_INIT	 9		/* initialized only */
/*
	A file for which	no stat(2) information was available.
	The contents of the fts_statp field are undefined.
	This is an error return, and the fts_errno field will
	be set to indicate what caused the error.
*/
#define	FTS_NS		10		/* stat(2) failed */
/*
	A file for which no stat(2) information was requested.
	The contents of the fts_statp field are undefined.
*/
#define	FTS_NSOK	11		/* no stat(2) requested */
#define	FTS_SL		12		/* symbolic link - 有目标文件的符号链接 */
#define	FTS_SLNONE	13		/* symbolic link without target - 没有目标文件的符号链接 */
#define	FTS_W		14		/* whiteout object */
	int fts_info;			/* user status for FTSENT structure */

#define	FTS_DONTCHDIR	 0x01		/* don't chdir .. to the parent */
#define	FTS_SYMFOLLOW	 0x02		/* followed a symlink to get here */
#define	FTS_ISW		 0x04		/* this is a whiteout object */
	unsigned fts_flags;		/* private flags for FTSENT structure */

/*
	Re-visit the file; any file type may be re-visited. The next call to fts_read()
	will return the referenced file. The fts_stat	and fts_info fields of the structure
	will be reinitialized at that time, but no other fields will have been changed.
	This option is meaningful only for the most recently returned file from fts_read().
	Normal use is for post-order directory visits, where it causes the directory to be
	re-visited (in both pre and post-order) as well as all of its descendants.
*/
#define	FTS_AGAIN	 1		/* read node again */
/*
	The referenced file must be a symbolic link. If the	referenced file
	is the one most recently returned	by fts_read(), the next call to
	fts_read() returns the file	with the fts_info and	fts_statp fields
	reinitialized to reflect the target of the symbolic link instead of
	the symbolic link itself. If the file	is one of those	most recently
	returned by fts_children(), the fts_info and fts_statp fields	of the
	structure, when returned by fts_read(), will reflect the target of
	the symbolic link instead of the symbolic link itself. In either case,
	if the target of the symbolic link does	not exist the fields of	the
	returned structure will be unchanged and the fts_info field will be
	set	to FTS_SLNONE.
	If the target of the link is a directory, the pre-order return,
	followed by the return of all of its descendants, followed by
	a post-order return, is done.
*/
#define	FTS_FOLLOW	 2		/* follow symbolic link */
#define	FTS_NOINSTR	 3		/* no instructions */
/*
	No descendants of this file are visited. The file may be one of those
	most recently returned by either fts_children() or fts_read().
*/
#define	FTS_SKIP	 4		/* discard node */
	int fts_instr;			/* fts_set() instructions */
	/*
		A pointer to stat(2) information for the file.
	*/
	struct stat *fts_statp;		/* stat(2) information */
	char *fts_name;			/* file name */
	FTS *fts_fts;			/* back pointer to main FTS */
} FTSENT;

#include <sys/cdefs.h>

__BEGIN_DECLS
/*
	In general, directories are	visited	two distinguishable times;
	in pre-order (before any of their descendants are visited) and
	in post-order (after all of their descendants have been visited).
	Files are visited once. It is possible to walk the hierarchy	
	"logically" (ignoring symbolic links) or physically (visiting
	symbolic links), order the walk of the hierarchy or prune and/or
	re-visit portions of the hierarchy.
*/
/*
	The function fts_children() returns a pointer to a linked list
	of structures, each of which describes one of the files contained
	in a directory in the hierarchy.
*/
FTSENT	*fts_children(FTS *, int);
int	 fts_close(FTS *);
void	*fts_get_clientptr(FTS *);
#define	 fts_get_clientptr(fts)	((fts)->fts_clientptr)
FTS	*fts_get_stream(FTSENT *);
#define	 fts_get_stream(ftsent)	((ftsent)->fts_fts)
/*
	fts_open() function returns a "handle" on a file hierarchy,
	which is then supplied to the other fts functions.
	作用类似于通过 fopen() 获取文件描述符

	The fts_open() function takes a pointer to an array of character
	pointers naming one	or more	paths which make up a logical file
	hierarchy to be traversed. The array must	be terminated by a
	NULL pointer.
*/
FTS	*fts_open(char * const *, int,
	    int (*)(const FTSENT * const *, const FTSENT * const *));
/*
	The function fts_read() returns a pointer to a structure
	describing one of the files in the file hierarchy.
	A single buffer is used for all of the paths of all of the files
	in the file hierarchy. Therefore, the fts_path and fts_accpath
	fields are guaranteed to be NUL-terminated only for the file most
	recently returned by fts_read(). To use these fields to reference
	any files represented by other FTSENT structures will require that
	the path buffer be modified using the information contained in that
	FTSENT structure's fts_pathlen field. Any such modifications should
	be undone before further calls to fts_read() are attempted.	
	The fts_name field is always NUL-terminated.
*/
FTSENT	*fts_read(FTS *);
/*
	The function fts_set() allows the user application	to determine further
  processing for the file f of the stream ftsp. The	fts_set() function returns
	0 on, and -1	if an error occurs.
*/
int	 fts_set(FTS *, FTSENT *, int);
/*
	The FTS structure contains space for a single pointer, which may
	be used to store application data or per-hierarchy state.
	The fts_set_clientptr() and fts_get_clientptr() functions may be
	used to set and retrieve this pointer. This is likely	to be useful
	only when accessed from the sort comparison function, which can
	determine the original FTS stream of its arguments using the
	fts_get_stream() function. The two get functions are also	available
	as macros of the same	name.
*/
void	 fts_set_clientptr(FTS *, void *);
__END_DECLS

#endif /* !_FTS_H_ */
