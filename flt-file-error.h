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

#ifndef FLT_FILE_ERROR_H
#define FLT_FILE_ERROR_H

#include "flt-error.h"

extern struct flt_error_domain
flt_file_error;

enum flt_file_error {
        FLT_FILE_ERROR_EXIST,
        FLT_FILE_ERROR_ISDIR,
        FLT_FILE_ERROR_ACCES,
        FLT_FILE_ERROR_NAMETOOLONG,
        FLT_FILE_ERROR_NOENT,
        FLT_FILE_ERROR_NOTDIR,
        FLT_FILE_ERROR_AGAIN,
        FLT_FILE_ERROR_INTR,
        FLT_FILE_ERROR_PERM,
        FLT_FILE_ERROR_PFNOSUPPORT,
        FLT_FILE_ERROR_AFNOSUPPORT,
        FLT_FILE_ERROR_MFILE,

        FLT_FILE_ERROR_OTHER
};

enum flt_file_error
flt_file_error_from_errno(int errnum);

FLT_PRINTF_FORMAT(3, 4) void
flt_file_error_set(struct flt_error **error,
                   int errnum,
                   const char *format,
                   ...);

#endif /* FLT_FILE_ERROR_H */
