/*  Audacious
 *  Copyright (c) 2006 William Pitcock
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <audacious/vfs.h>
#include <audacious/plugin.h>
#include <stdio.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

VFSFile *
stdio_vfs_fopen_impl(const gchar * path,
          const gchar * mode)
{
    VFSFile *file;

    if (!path || !mode)
	return NULL;

    file = g_new(VFSFile, 1);

    file->handle = fopen(path, mode);

    if (file->handle == NULL) {
        g_free(file);
        file = NULL;
    }

    return file;
}

gint
stdio_vfs_fclose_impl(VFSFile * file)
{
    gint ret = 0;

    if (file == NULL)
        return -1;

    if (file->handle)
    {
        FILE *handle = (FILE *) file->handle;

        if (fclose(handle) != 0)
            ret = -1;
    }

    return ret;
}

size_t
stdio_vfs_fread_impl(gpointer ptr,
          size_t size,
          size_t nmemb,
          VFSFile * file)
{
    FILE *handle;

    if (file == NULL)
        return 0;

    handle = (FILE *) file->handle;

    return fread(ptr, size, nmemb, handle);
}

size_t
stdio_vfs_fwrite_impl(gconstpointer ptr,
           size_t size,
           size_t nmemb,
           VFSFile * file)
{
    FILE *handle;

    if (file == NULL)
        return 0;

    handle = (FILE *) file->handle;

    return fwrite(ptr, size, nmemb, handle);
}

gint
stdio_vfs_getc_impl(VFSFile *stream)
{
  FILE *handle = (FILE *) stream->handle;

  return getc( handle );
}

gint
stdio_vfs_ungetc_impl(gint c, VFSFile *stream)
{
  FILE *handle = (FILE *) stream->handle;

  return ungetc( c , handle );
}

gint
stdio_vfs_fseek_impl(VFSFile * file,
          glong offset,
          gint whence)
{
    FILE *handle;

    if (file == NULL)
        return 0;

    handle = (FILE *) file->handle;

    return fseek(handle, offset, whence);
}

void
stdio_vfs_rewind_impl(VFSFile * file)
{
    FILE *handle;

    if (file == NULL)
        return;

    handle = (FILE *) file->handle;

    rewind(handle);
}

glong
stdio_vfs_ftell_impl(VFSFile * file)
{
    FILE *handle;

    if (file == NULL)
        return 0;

    handle = (FILE *) file->handle;

    return ftell(handle);
}

gboolean
stdio_vfs_feof_impl(VFSFile * file)
{
    FILE *handle;

    if (file == NULL)
        return FALSE;

    handle = (FILE *) file->handle;

    return (gboolean) feof(handle);
}

gint
stdio_vfs_truncate_impl(VFSFile * file, glong size)
{
    FILE *handle;

    if (file == NULL)
        return -1;

    handle = (FILE *) file->handle;

    return ftruncate(fileno(handle), size);
}

VFSConstructor file_const = {
	"file://",
	stdio_vfs_fopen_impl,
	stdio_vfs_fclose_impl,
	stdio_vfs_fread_impl,
	stdio_vfs_fwrite_impl,
	stdio_vfs_getc_impl,
	stdio_vfs_ungetc_impl,
	stdio_vfs_fseek_impl,
	stdio_vfs_rewind_impl,
	stdio_vfs_ftell_impl,
	stdio_vfs_feof_impl,
	stdio_vfs_truncate_impl
};

VFSConstructor default_const = {
	"/",
	stdio_vfs_fopen_impl,
	stdio_vfs_fclose_impl,
	stdio_vfs_fread_impl,
	stdio_vfs_fwrite_impl,
	stdio_vfs_getc_impl,
	stdio_vfs_ungetc_impl,
	stdio_vfs_fseek_impl,
	stdio_vfs_rewind_impl,
	stdio_vfs_ftell_impl,
	stdio_vfs_feof_impl,
	stdio_vfs_truncate_impl
};

static void init(void)
{
	vfs_register_transport(&default_const);
	vfs_register_transport(&file_const);
}

static void cleanup(void)
{
#if 0
	vfs_unregister_transport(&default_const);
	vfs_unregister_transport(&file_const);
#endif
}

LowlevelPlugin llp_stdio = {
	NULL,
	NULL,
	"file:// URI Transport",
	init,
	cleanup,
};

LowlevelPlugin *get_lplugin_info(void)
{
        return &llp_stdio;
}

