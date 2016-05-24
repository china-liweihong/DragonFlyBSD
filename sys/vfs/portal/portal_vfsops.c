/*
 * Copyright (c) 1992, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)portal_vfsops.c	8.11 (Berkeley) 5/14/95
 *
 * $FreeBSD: src/sys/miscfs/portal/portal_vfsops.c,v 1.26.2.2 2001/07/26 20:37:16 iedowse Exp $
 */

/*
 * Portal Filesystem
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/filedesc.h>
#include <sys/file.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include "portal.h"

extern struct vop_ops portal_vnode_vops;

static MALLOC_DEFINE(M_PORTALFSMNT, "PORTAL mount", "PORTAL mount structure");

static int	portal_mount (struct mount *mp, char *path, caddr_t data,
				  struct ucred *cred);
static int	portal_unmount (struct mount *mp, int mntflags);
static int	portal_root (struct mount *mp, struct vnode **vpp);
static int	portal_statfs (struct mount *mp, struct statfs *sbp,
				struct ucred *);

/*
 * Mount the per-process file descriptors (/dev/fd)
 */
static int
portal_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	struct file *fp;
	struct portal_args args;
	struct portalmount *fmp;
	struct socket *so;
	struct vnode *rvp;
	struct portalnode *pn;
	size_t size;
	int error;

	/*
	 * Update is a no-op
	 */
	if (mp->mnt_flag & MNT_UPDATE)
		return (EOPNOTSUPP);

	error = copyin(data, (caddr_t) &args, sizeof(struct portal_args));
	if (error)
		return (error);

	KKASSERT(curproc != NULL);
	error = holdsock(curproc->p_fd, args.pa_socket, &fp);
	if (error)
		return (error);
	so = (struct socket *) fp->f_data;
	if (so->so_proto->pr_domain->dom_family != AF_UNIX) {
		fdrop(fp);
		return (ESOCKTNOSUPPORT);
	}

	pn = kmalloc(sizeof(struct portalnode), M_TEMP, M_WAITOK);

	fmp = kmalloc(sizeof(struct portalmount), M_PORTALFSMNT, M_WAITOK);	/* XXX */

	 
	vfs_add_vnodeops(mp, &portal_vnode_vops, &mp->mnt_vn_norm_ops);

	error = getnewvnode(VT_PORTAL, mp, &rvp, 0, 0);
	if (error) {
		kfree(fmp, M_PORTALFSMNT);
		kfree(pn, M_TEMP);
		fdrop(fp);
		return (error);
	}

	rvp->v_data = pn;
	rvp->v_type = VDIR;
	vsetflags(rvp, VROOT);
	VTOPORTAL(rvp)->pt_arg = 0;
	VTOPORTAL(rvp)->pt_size = 0;
	VTOPORTAL(rvp)->pt_fileid = PORTAL_ROOTFILEID;
	fmp->pm_root = rvp;
	fmp->pm_server = fp;
	fhold(fp);

	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_data = (qaddr_t) fmp;
	vfs_getnewfsid(mp);

	(void)copyinstr(args.pa_config,
	    mp->mnt_stat.f_mntfromname, MNAMELEN - 1, &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);

#ifdef notdef
	bzero(mp->mnt_stat.f_mntfromname, MNAMELEN);
	bcopy("portal", mp->mnt_stat.f_mntfromname, sizeof("portal"));
#endif
	vx_unlock(rvp);

	(void)portal_statfs(mp, &mp->mnt_stat, cred);
	fdrop(fp);
	return (0);
}

static int
portal_unmount(struct mount *mp, int mntflags)
{
	int error, flags = 0;


	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	/*
	 * Clear out buffer cache.  I don't think we
	 * ever get anything cached at this level at the
	 * moment, but who knows...
	 */
#ifdef notyet
	mntflushbuf(mp, 0);
	if (mntinvalbuf(mp, 1))
		return (EBUSY);
#endif
	/* There is 1 extra root vnode reference (pm_root). */
	error = vflush(mp, 1, flags);
	if (error)
		return (error);

	/*
	 * Shutdown the socket.  This will cause the select in the
	 * daemon to wake up, and then the accept will get ECONNABORTED
	 * which it interprets as a request to go and bury itself.
	 */
	soshutdown((struct socket *) VFSTOPORTAL(mp)->pm_server->f_data,
		SHUT_RDWR);
	/*
	 * Discard reference to underlying file.  Must call closef because
	 * this may be the last reference.
	 */
	closef(VFSTOPORTAL(mp)->pm_server, NULL);
	/*
	 * Finally, throw away the portalmount structure
	 */
	kfree(mp->mnt_data, M_PORTALFSMNT);	/* XXX */
	mp->mnt_data = 0;
	return (0);
}

static int
portal_root(struct mount *mp, struct vnode **vpp)
{
	struct vnode *vp;
	int error;

	/*
	 * Return locked reference to root.
	 */
	vp = VFSTOPORTAL(mp)->pm_root;
	error = vget(vp, LK_EXCLUSIVE | LK_RETRY);
	if (error == 0)
		*vpp = vp;
	return (error);
}

static int
portal_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	sbp->f_flags = 0;
	sbp->f_bsize = DEV_BSIZE;
	sbp->f_iosize = DEV_BSIZE;
	sbp->f_blocks = 2;		/* 1K to keep df happy */
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 1;		/* Allow for "." */
	sbp->f_ffree = 0;		/* See comments above */
	if (sbp != &mp->mnt_stat) {
		sbp->f_type = mp->mnt_vfc->vfc_typenum;
		bcopy(&mp->mnt_stat.f_fsid, &sbp->f_fsid, sizeof(sbp->f_fsid));
		bcopy(mp->mnt_stat.f_mntfromname, sbp->f_mntfromname, MNAMELEN);
	}
	return (0);
}

static struct vfsops portal_vfsops = {
	.vfs_mount =    	portal_mount,
	.vfs_unmount =  	portal_unmount,
	.vfs_root =     	portal_root,
	.vfs_statfs =   	portal_statfs,
};

VFS_SET(portal_vfsops, portal, VFCF_SYNTHETIC);
MODULE_VERSION(portal, 1);
