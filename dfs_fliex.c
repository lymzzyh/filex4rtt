#include <rtdevice.h>
#include <rtthread.h>

#include <dfs_file.h>
#include <dfs_fs.h>

#include "fx_api.h"
#include "fx_directory.h"

#include <stdio.h>
#include <string.h>

#ifndef FLIEX_MEDIA_MEMORY_SIZE
#define FLIEX_MEDIA_MEMORY_SIZE 512     /* Size */
#endif /* FLIEX_MEDIA_MEMORY_SIZE */

typedef struct filex_media {
    rt_list_t list;
    FX_MEDIA media;
    char media_memory[FLIEX_MEDIA_MEMORY_SIZE];
#ifdef FX_ENABLE_FAULT_TOLERANT
    char fault_tolerant_memory[FLIEX_MEDIA_MEMORY_SIZE];
#endif
} filex_media_t;

typedef struct filex_dir {
    FX_DIR_ENTRY entry;
    filex_media_t * media;
} filex_dir_t;

rt_list_t filex_media_list;

static filex_media_t * _filex_get_media(rt_device_t dev_id)
{
    list_t * entry;
    filex_media_t * media;
    for(entry = filex_media_list.next; entry->next != &filex_media_list; entry = entry->next) 
    {
        media = rt_list_entry(entry, filex_media_t, list);
        if(media->media.fx_media_driver_info == dev_id)
        {
            return media;
        }
    }
    return NULL;
}

static int _filex_result_to_dfs(int result)
{
    int status = 0;

    switch (result)
    {
    case FX_SUCCESS:
        break;

    case FX_IO_ERROR:
        status = -EIO;
        break; // Error during device operation

    case FX_NOT_DIRECTORY:
        status = -ENOENT;
        break; // No directory entry

    case FX_ALREADY_CREATED:
        status = -EEXIST;
        break; // Entry already exists

    case FX_NOT_DIRECTORY:
        status = -ENOTDIR;
        break; // Entry is not a dir

    case FX_NOT_A_FILE:
        status = -EISDIR;
        break; // Entry is a dir

    case FX_DIR_NOT_EMPTY:
        status = -ENOTEMPTY;
        break; // Dir is not empty

    case FX_INVALID_OPTION:
        status = -EINVAL;
        break; // Invalid parameter

    case FX_NO_MORE_SPACE:
        status = -ENOSPC;
        break; // No space left on device

    case FX_NOT_ENOUGH_MEMORY:
        status = -ENOMEM;
        break; // No more memory available

    case FX_FILE_CORRUPT:
        status = -52;
        break; // Corrupted

    default:
        break;
    }

    return status;
}


static int _dfs_filex_mount(struct dfs_filesystem* dfs, unsigned long rwflag, const void* data)
{
    int result;
    rt_device_t dev_id = dfs->dev_id;
    filex_media_t * filex_media;

    /* Check Device Type */
    if (dev_id->type != RT_Device_Class_MTD && dev_id->type != RT_Device_Class_Block)
    {
        rt_kprintf("The memory device type must be MTD or Block!\n");
        return -EINVAL;
    }
    /* if do mkfs */
    filex_media = _filex_get_media(dev_id);
    if(filex_media == NULL)
    {
        filex_media = malloc(sizeof(filex_media_t));
        if(filex_media == NULL) 
        {
            return -ENOMEM;
        }
        rt_list_insert_before(&filex_media_list, &filex_media->list);
    }

    result =  fx_media_open(&filex_media->media, dev_id->parent.name, rt_fx_disk_driver, dev_id, filex_media->media_memory, sizeof(filex_media->media_memory));

    /* Check the media open status.  */
    if (result != FX_SUCCESS)
    {

        /* Error, break the loop!  */
        rt_list_remove(&filex_media->list);
        free(filex_media);
        return _filex_result_to_dfs(result);
    }

    dfs->data = filex_media;

    return RT_EOK;
}

static int _dfs_filex_unmount(struct dfs_filesystem* dfs)
{
    int result;
    filex_media_t * filex_media;

    RT_ASSERT(dfs != RT_NULL);
    RT_ASSERT(dfs->data != RT_NULL);

    filex_media = (filex_media_t*)dfs->data;
    result =  fx_media_close(&filex_media->media);
    
    if (result == FX_SUCCESS)
    {
        dfs->data = NULL;
        rt_list_remove(&filex_media->list);
        free(filex_media);
    }

    return _filex_result_to_dfs(result);
}



static int _dfs_filex_fat_mkfs(rt_device_t dev_id)
{
    uint32_t sectors_count;
    uint32_t sectors_begin;
    uint32_t sectors_size;
    filex_media_t * filex_media;
    if(dev_id == RT_NULL)
    {
        rt_kprintf("dev_id is NULL %s,%d\n", __func__, __LINE__);
        return -EINVAL;
    }
    /* Check Device Type */
    if (dev_id->type != RT_Device_Class_MTD && dev_id->type != RT_Device_Class_Block)
    {
        rt_kprintf("The memory device type must be MTD or Block!\n");
        return -EINVAL;
    }

    switch(dev_id->type)
    {
    case RT_Device_Class_MTD:
        sectors_count = RT_MTD_NOR_DEVICE(dev_id)->block_start - RT_MTD_NOR_DEVICE(dev_id)->block_end;
        sectors_begin = RT_MTD_NOR_DEVICE(dev_id)->block_start;
        sectors_size = RT_MTD_NOR_DEVICE(dev_id)->block_size;
        break;
    case RT_Device_Class_Block:
        {
            struct rt_device_blk_geometry geometry;
            rt_memset(&geometry, 0, sizeof(geometry));
            result = rt_device_control(dev_id,
                                    RT_DEVICE_CTRL_BLK_GETGEOME,
                                    &geometry);
            if( result != RT_EOK )
            {
                rt_kprintf("device : %s cmd RT_DEVICE_CTRL_BLK_GETGEOME failed.\r\n");
                return result;
            }
            sectors_count = geometry.sector_count;
            sectors_size = geometry.bytes_per_sector;
            sectors_begin = 0;
            break;
        }
    default:
        return -EINVAL;
    }
    filex_media = _filex_get_media(dev_id);
    if(filex_media == NULL)
    {
        filex_media = malloc(sizeof(filex_media_t));
        if(filex_media == NULL) 
        {
            return -ENOMEM;
        }
        rt_list_insert_before(&filex_media_list, &filex_media->list);
    }
    
    result = fx_media_format(&filex_media->media,
                    rt_fx_disk_driver,               // Driver entry
                    dev_id,              // RAM disk memory pointer
                    filex_media->media_memory,                 // Media buffer pointer
                    sizeof(filex_media->media_memory),         // Media buffer size
                    dev_id->parent.name,                // Volume Name
                    1,                            // Number of FATs
                    32,                           // Directory Entries
                    sectors_begin,                            // Hidden sectors
                    sectors_count,                          // Total sectors
                    sectors_size,                          // Sector size
                    8,                            // Sectors per cluster
                    1,                            // Heads
                    1);                           // Sectors per track

    if (result != FX_SUCCESS)
    {

        /* Error, break the loop!  */
        rt_list_remove(&filex_media->list);
        free(filex_media);
        return _filex_result_to_dfs(result);
    }

#ifdef FX_ENABLE_FAULT_TOLERANT
        result = fx_fault_tolerant_enable(&filex_media->media, filex_media->fault_tolerant_memory, sizeof(filex_media->fault_tolerant_memory));

        if (result != FX_SUCCESS)
        {

            /* Error, break the loop!  */
            rt_list_remove(&filex_media->list);
            free(filex_media);
        }
#endif /* FX_ENABLE_FAULT_TOLERANT */

    return _filex_result_to_dfs(result);
    
}

#ifdef FX_ENABLE_EXFAT
static int _dfs_filex_exfat_mkfs(rt_device_t dev_id)
{
    uint32_t sectors_count;
    uint32_t sectors_begin;
    uint32_t sectors_size;
    filex_media_t * filex_media;
    if(dev_id == RT_NULL)
    {
        rt_kprintf("dev_id is NULL %s,%d\n", __func__, __LINE__);
        return -EINVAL;
    }
    /* Check Device Type */
    if (dev_id->type != RT_Device_Class_MTD && dev_id->type != RT_Device_Class_Block)
    {
        rt_kprintf("The memory device type must be MTD or Block!\n");
        return -EINVAL;
    }

    switch(dev_id->type)
    {
    case RT_Device_Class_MTD:
        sectors_count = RT_MTD_NOR_DEVICE(dev_id)->block_start - RT_MTD_NOR_DEVICE(dev_id)->block_end;
        sectors_begin = RT_MTD_NOR_DEVICE(dev_id)->block_start;
        sectors_size = RT_MTD_NOR_DEVICE(dev_id)->block_size;
        break;
    case RT_Device_Class_Block:
        {
            struct rt_device_blk_geometry geometry;
            rt_memset(&geometry, 0, sizeof(geometry));
            result = rt_device_control(dev_id,
                                    RT_DEVICE_CTRL_BLK_GETGEOME,
                                    &geometry);
            if( result != RT_EOK )
            {
                rt_kprintf("device : %s cmd RT_DEVICE_CTRL_BLK_GETGEOME failed.\r\n");
                return result;
            }
            sectors_count = geometry.sector_count;
            sectors_size = geometry.bytes_per_sector;
            sectors_begin = 0;
            break;
        }
    default:
        return -EINVAL;
    }
    filex_media = _filex_get_media(dev_id);
    if(filex_media == NULL)
    {
        filex_media = malloc(sizeof(filex_media_t));
        if(filex_media == NULL) 
        {
            return -ENOMEM;
        }
        rt_list_insert_before(&filex_media_list, &filex_media->list);
    }

    result = fx_media_exFAT_format(&filex_media->media,
                          rt_fx_disk_driver,         // Driver entry
                          dev_id,        // RAM disk memory pointer
                          filex_media->media_memory,           // Media buffer pointer
                          sizeof(filex_media->media_memory),   // Media buffer size
                          dev_id->parent.name,          // Volume Name
                          1,                      // Number of FATs
                          sectors_begin,                      // Hidden sectors
                          sectors_count,                    // Total sectors
                          sectors_size,                    // Sector size
                          8,                      // exFAT Sectors per cluster
                          12345,                  // Volume ID
                          1);                     // Boundary unit
    if (result != FX_SUCCESS)
    {

        /* Error, break the loop!  */
        rt_list_remove(&filex_media->list);
        free(filex_media);
        return _filex_result_to_dfs(result);
    }

#ifdef FX_ENABLE_FAULT_TOLERANT
        result = fx_fault_tolerant_enable(&filex_media->media, filex_media->fault_tolerant_memory, sizeof(filex_media->fault_tolerant_memory));

        if (result != FX_SUCCESS)
        {

            /* Error, break the loop!  */
            rt_list_remove(&filex_media->list);
            free(filex_media);
        }
#endif /* FX_ENABLE_FAULT_TOLERANT */
    
    return _filex_result_to_dfs(result);
    
}
#endif /* FX_ENABLE_EXFAT */


static int _dfs_filex_statfs(struct dfs_filesystem* dfs, struct statfs* buf)
{
    int result;
    filex_media_t * filex_media;
    ULONG64 available_bytes;

    RT_ASSERT(buf != RT_NULL);
    RT_ASSERT(dfs != RT_NULL);
    RT_ASSERT(dfs->data != RT_NULL);

    filex_media = (filex_media_t*)dfs->data;

    result = fx_media_extended_space_available(&filex_media->media, &available_bytes);

    if (result != FX_SUCCESS)
    {
        return _filex_result_to_dfs(result);
    }

    buf->f_bsize = filex_media->media.fx_media_bytes_per_sector;
    buf->f_blocks = filex_media->media.fx_media_total_sectors;
    buf->f_bfree = available_bytes / buf->f_bsize;

    return _filex_result_to_dfs(result);
}

static int _dfs_filex_unlink(struct dfs_filesystem* dfs, const char* path)
{
    filex_media_t * filex_media;
    int result;

    RT_ASSERT(dfs != RT_NULL);
    RT_ASSERT(dfs->data != RT_NULL);

    filex_media = (filex_media_t*)dfs->data;

    result = fx_file_delete(&filex_media->media, path);
    if(result == FX_NOT_A_FILE)
    {
        result = fx_directory_delete(&filex_media->media, path);
    }

    return _filex_result_to_dfs(result);
}

static int _dfs_filex_stat(struct dfs_filesystem* dfs, const char* path, struct stat* st)
{
    filex_media_t * filex_media;
    int result;
    FX_DIR_ENTRY dir_entry;

    RT_ASSERT(dfs != RT_NULL);
    RT_ASSERT(dfs->data != RT_NULL);
    RT_ASSERT(st != RT_NULL);

    filex_media = (filex_media_t*)dfs->data;

    result =  _fx_directory_search(&filex_media->media, path, &dir_entry, FX_NULL, FX_NULL);

    /* Determine if the search was successful.  */
    if (result != FX_SUCCESS)
    {
        /* Return the error code.  */
        return _filex_result_to_dfs(result);
    }

    memset(st, 0, sizeof(struct stat));
    st->st_mode = S_IRUSR | S_IRGRP | S_IROTH;

    if(!(dir_entry.fx_dir_entry_attributes & FX_READ_ONLY))
    {
        st->st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;
    }

    if(dir_entry.fx_dir_entry_attributes & FX_DIRECTORY)
    {
        st->st_mode |= S_IFDIR;
    }
    else
    {
        st->st_mode |= S_IFREG | S_IXUSR | S_IXGRP | S_IXOTH;
    }
    st->st_size = dir_entry.fx_dir_entry_file_size;
    st->st_atime =dir_entry.fx_dir_entry_last_accessed_date;
    st->st_mtime = dir_entry.fx_dir_entry_time;
    st->st_ctime = st->st_mtime;
    
    return _filex_result_to_dfs(FX_SUCCESS);
}

static int _dfs_filex_rename(struct dfs_filesystem* dfs, const char* from, const char* to)
{
    filex_media_t * filex_media;
    int result;

    RT_ASSERT(dfs != RT_NULL);
    RT_ASSERT(dfs->data != RT_NULL);

    filex_media = (filex_media_t*)dfs->data;

    result = fx_directory_rename(&filex_media->media, from, to);
    if(result == FX_NOT_DIRECTORY)
    {
        result = fx_file_rename(&filex_media->media, from, to);
    }

    return _filex_result_to_dfs(result);
}

/******************************************************************************
 * file operations
 ******************************************************************************/
static int _dfs_filex_open(struct dfs_fd* file)
{
    struct dfs_filesystem* dfs;
    filex_media_t * filex_media;
    int result;
    int flags = 0;

    RT_ASSERT(file != RT_NULL);
    RT_ASSERT(file->data != RT_NULL);

    dfs = (struct dfs_filesystem*)file->data;
    filex_media = (filex_media_t*)dfs->data;

    if (file->flags & O_DIRECTORY)
    {
        filex_dir_t *dir_entry = malloc(sizeof(filex_dir_t));

        if (dir_entry == NULL)
        {
            rt_kprintf("ERROR:no memory!\n");
            result = -ENOMEM;

            goto _error_dir;
        }

        if (file->flags & O_CREAT)
        {
            result = fx_directory_create(&filex_media->media, file->path);
            if (result != FX_SUCCESS)
            {
                goto _error_dir;
            }
        }
        result =  _fx_directory_search(&filex_media->media, path, &dir_entry->entry, FX_NULL, FX_NULL);

        /* Determine if the search was successful.  */
        if (result != FX_SUCCESS)
        {
            /* Return the error code.  */
            goto _error_dir;
        }
        else
        {
            file->data = (void*)dir_entry;
            return _filex_result_to_dfs(result);
        }

    _error_dir:
        if (dir_entry != NULL)
        {
            free(dir_entry);
        }
        return _filex_result_to_dfs(result);
    }
    else
    {
        FX_FILE* file_entry = malloc(sizeof(FX_FILE));
        if (file_entry == RT_NULL)
        {
            rt_kprintf("ERROR:no memory!\n");
            result = -ENOMEM;

            goto _error_file;
        }

        if ((file->flags & 3) == O_RDONLY)
            flags |= FX_OPEN_FOR_READ;
        if ((file->flags & 3) == O_WRONLY)
            flags |= FX_OPEN_FOR_WRITE;
        if ((file->flags & 3) == O_RDWR)
            flags |= FX_OPEN_FOR_READ | FX_OPEN_FOR_WRITE;

        if (file->flags & O_CREAT)
        {
            result = fx_file_create(&filex_media->media, file->path);
            if((file->flags & O_EXCL) && (result == FX_ALREADY_CREATED))
            {
                result = -EEXIST;
                goto _error_file;
            }
            if(result == FX_ALREADY_CREATED)
            {
                result = FX_SUCCESS;
            }
            if(result != FX_SUCCESS)
            {
                goto _error_file;
            }
        }

        result = fx_file_open(&filex_media->media, file_entry, file->path, flags);
        if (result != FX_SUCCESS)
        {
            goto _error_file;
        }
        else
        {
            if(file->flags & O_TRUNC)
            {
                fx_file_truncate_release(file_entry, 0);
            }
            if(file->flags & O_APPEND)
            {
                fx_file_relative_seek(file_entry, 0, FX_SEEK_END);
            }
            file->data = (void*)file_entry;
            file->pos = file_entry->fx_file_current_file_offset;
            file->size = file_entry->fx_file_current_file_size;
            return _filex_result_to_dfs(result);
        }

    _error_file:
        if (file_entry != RT_NULL)
        {
            free(file_entry);
        }
        return _filex_result_to_dfs(result);
    }
}

static int _dfs_filex_close(struct dfs_fd* file)
{
    int result;
    RT_ASSERT(file != RT_NULL);
    RT_ASSERT(file->data != RT_NULL);

    if (file->type == FT_DIRECTORY)
    {
        free(file->data);
        file->data = NULL;
        result = FX_SUCCESS;
    }
    else
    {
        FX_FILE* file_entry = (FX_FILE*)file->data;
        result = fx_file_close(file_entry);
        if(result == FX_SUCCESS)
        {
            free(file->data);
            file->data = NULL;
        }
    }

    return _filex_result_to_dfs(result);
}

static int _dfs_filex_ioctl(struct dfs_fd* file, int cmd, void* args)
{
    return -ENOSYS;
}

static int _dfs_filex_read(struct dfs_fd* file, void* buf, size_t len)
{
    FX_FILE* file_entry = (FX_FILE*)file->data;
    int result;
    int actual_size;

    RT_ASSERT(file != RT_NULL);
    RT_ASSERT(file->data != RT_NULL);


    if (file->type == FT_DIRECTORY)
    {
        return -EISDIR;
    }
    result = fx_file_read(file_entry, buf, len, &actual_size);
    if (result != FX_SUCCESS)
    {
        return _filex_result_to_dfs(result);
    }

    /* update position */
    file->pos = file_entry->fx_file_current_file_offset;

    return actual_size;
}

static int _dfs_filex_write(struct dfs_fd* file, const void* buf, size_t len)
{
    FX_FILE* file_entry = (FX_FILE*)file->data;
    int result;

    RT_ASSERT(file != RT_NULL);
    RT_ASSERT(file->data != RT_NULL);

    if (file->type == FT_DIRECTORY)
    {
        return -EISDIR;
    }

    result = fx_file_write(file_entry, buf, len);

    if (result != FX_SUCCESS)
    {
        return _filex_result_to_dfs(result);
    }

    /* update position and file size */
    file->pos = file_entry->fx_file_current_file_offset;
    file->size = file_entry->fx_file_current_file_size;

    return len;
}

static int _dfs_filex_flush(struct dfs_fd* file)
{
    FX_FILE* file_entry = (FX_FILE*)file->data;
    int result;

    RT_ASSERT(file != RT_NULL);
    RT_ASSERT(file->data != RT_NULL);

    result = fx_media_flush(file_entry->fx_file_media_ptr);


    return _filex_result_to_dfs(result);
}

static int _dfs_filex_lseek(struct dfs_fd* file, rt_off_t offset)
{
    int result;
    RT_ASSERT(file != RT_NULL);
    RT_ASSERT(file->data != RT_NULL);

    if (file->type == FT_REGULAR)
    {
        FX_FILE* file_entry = (FX_FILE*)file->data;
        result = fx_file_seek(file_entry, offset);
        if (result != FX_SUCCESS)
        {
            return _filex_result_to_dfs(result);
        }

        file->pos = file_entry->fx_file_current_file_offset;
    }
    else if (file->type == FT_DIRECTORY)
    {
        file->pos = offset;
    }

    return (file->pos);
}

static int _dfs_filex_getdents(struct dfs_fd* file, struct dirent* dirp, uint32_t count)
{
    filex_dir_t *dir_entry
    int result;
    int index;
    struct dirent* d;
    FX_DIR_ENTRY dest_entry;

    RT_ASSERT(file->data != RT_NULL);

    dir_entry = (filex_dir_t*)(file->data);

    /* make integer count */
    count = (count / sizeof(struct dirent)) * sizeof(struct dirent);
    if (count == 0)
    {
        return -EINVAL;
    }

    index = file->pos / sizeof(struct dirent);
    while (1)
    {
        d = dirp + index;
        
        
        result = _fx_directory_entry_read(dir_entry->media, &dir_entry->entry, index, &dest_entry);
        if ((result != FX_SUCCESS) || (dest_entry.fx_dir_entry_name[0] == 0))
        {
            break;
        }

        if (rt_strcmp(dest_entry.fx_dir_entry_name, ".") == 0)
        {
            continue;
        }
        else if (rt_strcmp(dest_entry.fx_dir_entry_name, "..") == 0)
        {
            continue;
        }

        d->d_type = DT_UNKNOWN;

        if(dest_entry.fx_dir_entry_attributes & FX_DIRECTORY)
        {
            d->d_type |= DT_DIR;
        }
        else
        {
            d->d_type |= DT_REG;
        }
        

        d->d_namlen = (rt_uint8_t)rt_strlen(dest_entry.fx_dir_entry_name);
        d->d_reclen = (rt_uint16_t)sizeof(struct dirent);
        strncpy(d->d_name, dest_entry.fx_dir_entry_name, d->d_namlen + 1);

        index++;
        if (index * sizeof(struct dirent) >= count)
        {
            break;
        }
    }

    if (index == file->pos)
    {
        return _lfs_result_to_dfs(result);
    }

    file->pos += index * sizeof(struct dirent);

    return count * sizeof(struct dirent);
}

static const struct dfs_file_ops _dfs_filex_fops = {
    _dfs_filex_open,
    _dfs_filex_close,
    _dfs_filex_ioctl,
    _dfs_filex_read,
    _dfs_filex_write,
    _dfs_filex_flush,
    _dfs_filex_lseek,
    _dfs_filex_getdents,
    //    RT_NULL, /* poll interface */
};

static const struct dfs_filesystem_ops _dfs_filex_fat_ops = {
    "fat",
    DFS_FS_FLAG_DEFAULT,
    &_dfs_filex_fops,

    _dfs_filex_mount,
    _dfs_filex_unmount,
    _dfs_filex_fat_mkfs,
    _dfs_filex_statfs,
    _dfs_filex_unlink,
    _dfs_filex_stat,
    _dfs_filex_rename,
};

#ifdef FX_ENABLE_EXFAT
static const struct dfs_filesystem_ops _dfs_filex_exfat_ops = {
    "exfat",
    DFS_FS_FLAG_DEFAULT,
    &_dfs_filex_fops,

    _dfs_filex_mount,
    _dfs_filex_unmount,
    _dfs_filex_exfat_mkfs,
    _dfs_filex_statfs,
    _dfs_filex_unlink,
    _dfs_filex_stat,
    _dfs_filex_rename,
};

#endif

int dfs_lfs_init(void)
{
#ifdef FX_ENABLE_EXFAT
    dfs_register(&_dfs_filex_exfat_ops);
#endif
    rt_list_init(&filex_media_list);
    /* register ram file system */
    return dfs_register(&_dfs_filex_fat_ops);
}
INIT_COMPONENT_EXPORT(dfs_lfs_init);
