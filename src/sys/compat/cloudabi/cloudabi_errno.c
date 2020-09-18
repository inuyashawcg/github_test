/*-
 * Copyright (c) 2015 Nuxi, https://nuxi.nl/
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/compat/cloudabi/cloudabi_errno.c 297247 2016-03-24 21:47:15Z ed $");

#include <sys/param.h>

#include <contrib/cloudabi/cloudabi_types_common.h>

#include <compat/cloudabi/cloudabi_util.h>

/* Converts a FreeBSD errno to a CloudABI errno. */
cloudabi_errno_t
cloudabi_convert_errno(int error)
{
	static const int table[] = {
		[E2BIG]			= CLOUDABI_E2BIG,
		[EACCES]		= CLOUDABI_EACCES,
		[EADDRINUSE]		= CLOUDABI_EADDRINUSE,
		[EADDRNOTAVAIL]		= CLOUDABI_EADDRNOTAVAIL,
		[EAFNOSUPPORT]		= CLOUDABI_EAFNOSUPPORT,
		[EAGAIN]		= CLOUDABI_EAGAIN,
		[EALREADY]		= CLOUDABI_EALREADY,
		[EBADF]			= CLOUDABI_EBADF,
		[EBADMSG]		= CLOUDABI_EBADMSG,
		[EBUSY]			= CLOUDABI_EBUSY,
		[ECANCELED]		= CLOUDABI_ECANCELED,
		[ECHILD]		= CLOUDABI_ECHILD,
		[ECONNABORTED]		= CLOUDABI_ECONNABORTED,
		[ECONNREFUSED]		= CLOUDABI_ECONNREFUSED,
		[ECONNRESET]		= CLOUDABI_ECONNRESET,
		[EDEADLK]		= CLOUDABI_EDEADLK,
		[EDESTADDRREQ]		= CLOUDABI_EDESTADDRREQ,
		[EDOM]			= CLOUDABI_EDOM,
		[EDQUOT]		= CLOUDABI_EDQUOT,
		[EEXIST]		= CLOUDABI_EEXIST,
		[EFAULT]		= CLOUDABI_EFAULT,
		[EFBIG]			= CLOUDABI_EFBIG,
		[EHOSTUNREACH]		= CLOUDABI_EHOSTUNREACH,
		[EIDRM]			= CLOUDABI_EIDRM,
		[EILSEQ]		= CLOUDABI_EILSEQ,
		[EINPROGRESS]		= CLOUDABI_EINPROGRESS,
		[EINTR]			= CLOUDABI_EINTR,
		[EINVAL]		= CLOUDABI_EINVAL,
		[EIO]			= CLOUDABI_EIO,
		[EISCONN]		= CLOUDABI_EISCONN,
		[EISDIR]		= CLOUDABI_EISDIR,
		[ELOOP]			= CLOUDABI_ELOOP,
		[EMFILE]		= CLOUDABI_EMFILE,
		[EMLINK]		= CLOUDABI_EMLINK,
		[EMSGSIZE]		= CLOUDABI_EMSGSIZE,
		[EMULTIHOP]		= CLOUDABI_EMULTIHOP,
		[ENAMETOOLONG]		= CLOUDABI_ENAMETOOLONG,
		[ENETDOWN]		= CLOUDABI_ENETDOWN,
		[ENETRESET]		= CLOUDABI_ENETRESET,
		[ENETUNREACH]		= CLOUDABI_ENETUNREACH,
		[ENFILE]		= CLOUDABI_ENFILE,
		[ENOBUFS]		= CLOUDABI_ENOBUFS,
		[ENODEV]		= CLOUDABI_ENODEV,
		[ENOENT]		= CLOUDABI_ENOENT,
		[ENOEXEC]		= CLOUDABI_ENOEXEC,
		[ENOLCK]		= CLOUDABI_ENOLCK,
		[ENOLINK]		= CLOUDABI_ENOLINK,
		[ENOMEM]		= CLOUDABI_ENOMEM,
		[ENOMSG]		= CLOUDABI_ENOMSG,
		[ENOPROTOOPT]		= CLOUDABI_ENOPROTOOPT,
		[ENOSPC]		= CLOUDABI_ENOSPC,
		[ENOSYS]		= CLOUDABI_ENOSYS,
		[ENOTCONN]		= CLOUDABI_ENOTCONN,
		[ENOTDIR]		= CLOUDABI_ENOTDIR,
		[ENOTEMPTY]		= CLOUDABI_ENOTEMPTY,
		[ENOTRECOVERABLE]	= CLOUDABI_ENOTRECOVERABLE,
		[ENOTSOCK]		= CLOUDABI_ENOTSOCK,
		[ENOTSUP]		= CLOUDABI_ENOTSUP,
		[ENOTTY]		= CLOUDABI_ENOTTY,
		[ENXIO]			= CLOUDABI_ENXIO,
		[EOVERFLOW]		= CLOUDABI_EOVERFLOW,
		[EOWNERDEAD]		= CLOUDABI_EOWNERDEAD,
		[EPERM]			= CLOUDABI_EPERM,
		[EPIPE]			= CLOUDABI_EPIPE,
		[EPROTO]		= CLOUDABI_EPROTO,
		[EPROTONOSUPPORT]	= CLOUDABI_EPROTONOSUPPORT,
		[EPROTOTYPE]		= CLOUDABI_EPROTOTYPE,
		[ERANGE]		= CLOUDABI_ERANGE,
		[EROFS]			= CLOUDABI_EROFS,
		[ESPIPE]		= CLOUDABI_ESPIPE,
		[ESRCH]			= CLOUDABI_ESRCH,
		[ESTALE]		= CLOUDABI_ESTALE,
		[ETIMEDOUT]		= CLOUDABI_ETIMEDOUT,
		[ETXTBSY]		= CLOUDABI_ETXTBSY,
		[EXDEV]			= CLOUDABI_EXDEV,
		[ENOTCAPABLE]		= CLOUDABI_ENOTCAPABLE,
	};

	/* Unknown error: fall back to returning ENOSYS. */
	if (error < 0 || error >= nitems(table) || table[error] == 0)
		return (error == 0 ? 0 : CLOUDABI_ENOSYS);
	return (table[error]);
}