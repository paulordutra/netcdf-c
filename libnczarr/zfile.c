/* Copyright 2003-2018, University Corporation for Atmospheric
 * Research. See COPYRIGHT file for copying and redistribution
 * conditions. */

/**
 * @file
 * @internal The netCDF-4 file functions.
 *
 * This file is part of netcdf-4, a netCDF-like interface for NCZ, or
 * a ZARR backend for netCDF, depending on your point of view.
 *
 * @author Dennis Heimbigner, Ed Hartnett
 */

#include "zincludes.h"

/* Forward */
static int NCZ_enddef(int ncid);
static int ncz_sync_netcdf4_file(NC_FILE_INFO_T* file);

/**
 * @internal Put the file back in redef mode. This is done
 * automatically for netcdf-4 files, if the user forgets.
 *
 * @param ncid File and group ID.
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner, Ed Hartnett
 */
int
NCZ_redef(int ncid)
{
    NC_FILE_INFO_T* zinfo = NULL;
    int stat = NC_NOERR;

    LOG((1, "%s: ncid 0x%x", __func__, ncid));

    /* Find this file's metadata. */
    if ((stat = nc4_find_grp_h5(ncid, NULL, &zinfo)))
        return stat;
    assert(zinfo);

    /* If we're already in define mode, return an error. */
    if (zinfo->flags & NC_INDEF)
        return NC_EINDEFINE;

    /* If the file is read-only, return an error. */
    if (zinfo->no_write)
        return NC_EPERM;

    /* Set define mode. */
    zinfo->flags |= NC_INDEF;

    /* For nc_abort, we need to remember if we're in define mode as a
       redef. */
    zinfo->redef = NC_TRUE;

    return NC_NOERR;
}

/**
 * @internal For netcdf-4 files, this just calls nc_enddef, ignoring
 * the extra parameters.
 *
 * @param ncid File and group ID.
 * @param h_minfree Ignored for netCDF-4 files.
 * @param v_align Ignored for netCDF-4 files.
 * @param v_minfree Ignored for netCDF-4 files.
 * @param r_align Ignored for netCDF-4 files.
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner, Ed Hartnett
 */
int
NCZ__enddef(int ncid, size_t h_minfree, size_t v_align,
            size_t v_minfree, size_t r_align)
{
    return NCZ_enddef(ncid);
}

/**
 * @internal Take the file out of define mode. This is called
 * automatically for netcdf-4 files, if the user forgets.
 *
 * @param ncid File and group ID.
 *
 * @return ::NC_NOERR No error.
 * @return ::NC_EBADID Bad ncid.
 * @return ::NC_EBADGRPID Bad group ID.
 * @author Dennis Heimbigner, Ed Hartnett
 */
static int
NCZ_enddef(int ncid)
{
    NC_FILE_INFO_T* h5 = NULL;
    NC_GRP_INFO_T *grp;
    NC_VAR_INFO_T *var;
    int i;
    int stat = NC_NOERR;

    LOG((1, "%s: ncid 0x%x", __func__, ncid));

    /* Find pointer to group and zinfo. */
    if ((stat = nc4_find_grp_h5(ncid, &grp, &h5)))
        return stat;

    /* When exiting define mode, mark all variable written. */
    for (i = 0; i < ncindexsize(grp->vars); i++)
    {
        var = (NC_VAR_INFO_T *)ncindexith(grp->vars, i);
        assert(var);
        var->written_to = NC_TRUE;
    }
    return ncz_enddef_netcdf4_file(h5);
}

/**
 * @internal Flushes all buffers associated with the file, after
 * writing all changed metadata. This may only be called in data mode.
 *
 * @param ncid File and group ID.
 *
 * @return ::NC_NOERR No error.
 * @return ::NC_EBADID Bad ncid.
 * @return ::NC_EINDEFINE Classic model file is in define mode.
 * @author Dennis Heimbigner, Ed Hartnett
 */
int
NCZ_sync(int ncid)
{
    NC_FILE_INFO_T* file = NULL;
    int stat = NC_NOERR;

    LOG((2, "%s: ncid 0x%x", __func__, ncid));

    if ((stat = nc4_find_grp_h5(ncid, NULL, &file)))
        return stat;
    assert(file);

    /* If we're in define mode, we can't sync. */
    if (file->flags & NC_INDEF)
    {
        if (file->cmode & NC_CLASSIC_MODEL)
            return NC_EINDEFINE;
        if ((stat = NCZ_enddef(ncid)))
            return stat;
    }

    return ncz_sync_netcdf4_file(file);
}

/**
 * @internal From the netcdf-3 docs: The function nc_abort just closes
 * the netCDF dataset, if not in define mode. If the dataset is being
 * created and is still in define mode, the dataset is deleted. If
 * define mode was entered by a call to nc_redef, the netCDF dataset
 * is restored to its state before definition mode was entered and the
 * dataset is closed.
 *
 * @param ncid File and group ID.
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner, Ed Hartnett
 */
int
NCZ_abort(int ncid)
{
    int stat = NC_NOERR;
    NC_FILE_INFO_T* h5 = NULL;
    LOG((2, "%s: ncid 0x%x", __func__, ncid));
    /* Find metadata for this file. */
    if ((stat = nc4_find_grp_h5(ncid, NULL, &h5)))
        return stat;
    assert(h5);
    return ncz_closeorabort(h5, NULL, 1);
}

/**
 * @internal Close the netcdf file, writing any changes first.
 *
 * @param ncid File and group ID.
 * @param params any extra parameters in/out of close
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner, Ed Hartnett
 */
int
NCZ_close(int ncid, void* params)
{
    int stat = NC_NOERR;
    NC_FILE_INFO_T* h5 = NULL;
    LOG((1, "%s: ncid 0x%x", __func__, ncid));
    /* Find metadata for this file. */
    if ((stat = nc4_find_grp_h5(ncid, NULL, &h5)))
        return stat;
    assert(h5);
    return ncz_closeorabort(h5, params, 0);
}

/**
 * @internal From the netcdf-3 docs: The function nc_abort just closes
 * the netCDF dataset, if not in define mode. If the dataset is being
 * created and is still in define mode, the dataset is deleted. If
 * define mode was entered by a call to nc_redef, the netCDF dataset
 * is restored to its state before definition mode was entered and the
 * dataset is closed.
 *
 * @param ncid File and group ID.
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner, Ed Hartnett
 */
int
ncz_closeorabort(NC_FILE_INFO_T* h5, void* params, int abort)
{
    int stat = NC_NOERR;

    assert(h5);

    LOG((2, "%s: file: %p", __func__, h5));

    /* If we're in define mode, but not redefing the file, delete it. */
    if(!abort) {
	/* Invoke enddef if needed, which mean sync */
	if(h5->flags & NC_INDEF) h5->flags ^= NC_INDEF;
	/* Sync the file unless this is a read-only file. */
	if(!h5->no_write) {
	    if((stat = ncz_sync_netcdf4_file(h5)))
		goto done;
	}
    }

    /* Reclaim memory */

    /* Free any zarr-related data, including the map */
    if ((stat = ncz_close_file(h5, abort)))
	goto done;

    /* Reclaim provenance info */
    NCZ_clear_provenance(&h5->provenance);

    /* Free the NC_FILE_INFO_T struct. */
    if ((stat = nc4_nc4f_list_del(h5)))
        return stat;

done:
    return stat;
}

/**************************************************/
/**
 * @internal Learn number of dimensions, variables, global attributes,
 * and the ID of the first unlimited dimension (if any).
 *
 * @note It's possible for any of these pointers to be NULL, in which
 * case don't try to figure out that value.
 *
 * @param ncid File and group ID.
 * @param ndimsp Pointer that gets number of dimensions.
 * @param nvarsp Pointer that gets number of variables.
 * @param nattsp Pointer that gets number of global attributes.
 * @param unlimdimidp Pointer that gets first unlimited dimension ID,
 * or -1 if there are no unlimied dimensions.
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner, Ed Hartnett
 */
int
NCZ_inq(int ncid, int *ndimsp, int *nvarsp, int *nattsp, int *unlimdimidp)
{
    NC *nc;
    NC_FILE_INFO_T* file;
    NC_GRP_INFO_T *grp;
    int stat = NC_NOERR;
    int i;

    LOG((2, "%s: ncid 0x%x", __func__, ncid));

    /* Find file metadata. */
    if ((stat = nc4_find_nc_grp_h5(ncid, &nc, &grp, &file)))
        return stat;

    assert(file && grp && nc);

    /* Count the number of dims, vars, and global atts; need to iterate
     * because of possible nulls. */
    if (ndimsp)
    {
        *ndimsp = ncindexcount(grp->dim);
    }
    if (nvarsp)
    {
        *nvarsp = ncindexcount(grp->vars);
    }
    if (nattsp)
    {
        /* Do we need to read the atts? */
        if (!grp->atts_read)
            if ((stat = ncz_read_atts(file,(NC_OBJ*)grp)))
                return stat;

        *nattsp = ncindexcount(grp->att);
    }

    if (unlimdimidp)
    {
        /* Default, no unlimited dimension */
        *unlimdimidp = -1;

        /* If there's more than one unlimited dim, which was not possible
           with netcdf-3, then only the last unlimited one will be reported
           back in xtendimp. */
        /* Note that this code is inconsistent with nc_inq_unlimid() */
        for(i=0;i<ncindexsize(grp->dim);i++) {
            NC_DIM_INFO_T* d = (NC_DIM_INFO_T*)ncindexith(grp->dim,i);
            if(d == NULL) continue;
            if(d->unlimited) {
                *unlimdimidp = d->hdr.id;
                break;
            }
        }
    }

    return NC_NOERR;
}

/**
 * @internal This function will write all changed metadata and flush
 * ZARR file to disk.
 *
 * @param file Pointer to file info struct.
 *
 * @return ::NC_NOERR No error.
 * @return ::NC_EINDEFINE Classic model file in define mode.
 * @return ::NC_EHDFERR ZARR error.
 * @author Dennis Heimbigner, Ed Hartnett
 */

static int
ncz_sync_netcdf4_file(NC_FILE_INFO_T* file)
{
    int stat = NC_NOERR;

    assert(file && file->format_file_info);
    LOG((3, "%s", __func__));

    /* If we're in define mode, that's an error, for strict nc3 rules,
     * otherwise, end define mode. */
    if (file->flags & NC_INDEF)
    {
        if (file->cmode & NC_CLASSIC_MODEL)
            return NC_EINDEFINE;

        /* Turn define mode off. */
        file->flags ^= NC_INDEF;

        /* Redef mode needs to be tracked separately for nc_abort. */
        file->redef = NC_FALSE;
    }

#ifdef LOGGING
    /* This will print out the names, types, lens, etc of the vars and
       atts in the file, if the logging level is 2 or greater. */
    log_metadata_nc(file);
#endif

    /* Write any metadata that has changed. */
    if (!file->no_write)
    {
        /* Write out provenance; will create _NCProperties */
        if((stat = NCZ_write_provenance(file)))
            return stat;

        /* Write all the metadata. */
	if((stat = ncz_sync_file(file)))
	    return stat;
    }

    return NC_NOERR;
}

/**
 * @internal This function will do the enddef stuff for a netcdf-4 file.
 *
 * @param file Pointer to ZARR file info struct.
 *
 * @return ::NC_NOERR No error.
 * @return ::NC_ENOTINDEFINE Not in define mode.
 * @author Dennis Heimbigner, Ed Hartnett
 */
int
ncz_enddef_netcdf4_file(NC_FILE_INFO_T* file)
{
    assert(file);
    LOG((3, "%s", __func__));

    /* If we're not in define mode, return an error. */
    if (!(file->flags & NC_INDEF))
        return NC_ENOTINDEFINE;

    /* Turn define mode off. */
    file->flags ^= NC_INDEF;

    /* Redef mode needs to be tracked separately for nc_abort. */
    file->redef = NC_FALSE;

    return ncz_sync_netcdf4_file(file);
}

/**
 * @internal IN netcdf, you first create
 * the variable and then (optionally) specify the fill value.
 *
 * @param ncid File and group ID.
 * @param fillmode File mode.
 * @param old_modep Pointer that gets old mode. Ignored if NULL.
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner
 */
int
NCZ_set_fill(int ncid, int fillmode, int *old_modep)
{
    NC_FILE_INFO_T* h5 = NULL;
    int stat = NC_NOERR;

    LOG((2, "%s: ncid 0x%x fillmode %d", __func__, ncid, fillmode));

    /* Get pointer to file info. */
    if ((stat = nc4_find_grp_h5(ncid, NULL, &h5)))
        return stat;
    assert(h5);

    /* Trying to set fill on a read-only file? You sicken me! */
    if (h5->no_write)
        return NC_EPERM;

    /* Did you pass me some weird fillmode? */
    if (fillmode != NC_FILL && fillmode != NC_NOFILL)
        return NC_EINVAL;

    /* If the user wants to know, tell him what the old mode was. */
    if (old_modep)
        *old_modep = h5->fill_mode;

    h5->fill_mode = fillmode;

    return NC_NOERR;
}
