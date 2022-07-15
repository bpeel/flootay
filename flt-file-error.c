/*
 * Flootay – a video overlay generator
 * Copyright (C) 2013, 2020, 2022  Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "flt-file-error.h"

#include <errno.h>

struct flt_error_domain
flt_file_error;

enum flt_file_error
flt_file_error_from_errno(int errnum)
{
        switch (errnum) {
        case EEXIST:
                return FLT_FILE_ERROR_EXIST;
        case EISDIR:
                return FLT_FILE_ERROR_ISDIR;
        case EACCES:
                return FLT_FILE_ERROR_ACCES;
        case ENAMETOOLONG:
                return FLT_FILE_ERROR_NAMETOOLONG;
        case ENOENT:
                return FLT_FILE_ERROR_NOENT;
        case ENOTDIR:
                return FLT_FILE_ERROR_NOTDIR;
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
                return FLT_FILE_ERROR_AGAIN;
        case EINTR:
                return FLT_FILE_ERROR_INTR;
        case EPERM:
                return FLT_FILE_ERROR_PERM;
        case EMFILE:
                return FLT_FILE_ERROR_MFILE;
        }

        return FLT_FILE_ERROR_OTHER;
}

FLT_PRINTF_FORMAT(3, 4) void
flt_file_error_set(struct flt_error **error,
                   int errnum,
                   const char *format,
                   ...)
{
        va_list ap;

        va_start(ap, format);
        flt_set_error_va_list(error,
                              &flt_file_error,
                              flt_file_error_from_errno(errnum),
                              format,
                              ap);
        va_end(ap);
}
