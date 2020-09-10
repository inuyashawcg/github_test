/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include <nlm/sm_inter.h>
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: releng/12.0/sys/nlm/sm_inter_xdr.c 177685 2008-03-28 09:50:32Z dfr $");

bool_t
xdr_sm_name(XDR *xdrs, sm_name *objp)
{

	if (!xdr_string(xdrs, &objp->mon_name, SM_MAXSTRLEN))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_my_id(XDR *xdrs, my_id *objp)
{

	if (!xdr_string(xdrs, &objp->my_name, SM_MAXSTRLEN))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->my_prog))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->my_vers))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->my_proc))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_mon_id(XDR *xdrs, mon_id *objp)
{

	if (!xdr_string(xdrs, &objp->mon_name, SM_MAXSTRLEN))
		return (FALSE);
	if (!xdr_my_id(xdrs, &objp->my_id))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_mon(XDR *xdrs, mon *objp)
{

	if (!xdr_mon_id(xdrs, &objp->mon_id))
		return (FALSE);
	if (!xdr_opaque(xdrs, objp->priv, 16))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_stat_chge(XDR *xdrs, stat_chge *objp)
{

	if (!xdr_string(xdrs, &objp->mon_name, SM_MAXSTRLEN))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->state))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_sm_stat(XDR *xdrs, sm_stat *objp)
{

	if (!xdr_int(xdrs, &objp->state))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_sm_res(XDR *xdrs, sm_res *objp)
{

	if (!xdr_enum(xdrs, (enum_t *)objp))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_sm_stat_res(XDR *xdrs, sm_stat_res *objp)
{

	if (!xdr_sm_res(xdrs, &objp->res_stat))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->state))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_sm_status(XDR *xdrs, sm_status *objp)
{

	if (!xdr_string(xdrs, &objp->mon_name, SM_MAXSTRLEN))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->state))
		return (FALSE);
	if (!xdr_opaque(xdrs, objp->priv, 16))
		return (FALSE);
	return (TRUE);
}
