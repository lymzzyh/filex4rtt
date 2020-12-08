#include "fx_api.h"
#include "rtthread.h"

static size_t rt_disk_erase(rt_device_t disk_dev, off_t block_off, size_t number_of_block)
{
    size_t size;
    switch (disk_dev->type)
    {
    case RT_Device_Class_MTD:
        size = number_of_block * RT_MTD_NOR_DEVICE(disk_dev)->block_size;
        if(size != rt_mtd_nor_erase_block(RT_MTD_NOR_DEVICE(disk_dev), block_off * RT_MTD_NOR_DEVICE(disk_dev)->block_size, size))
        {
            return 0;
        }
        return number_of_block;
    case RT_Device_Class_Block:
        return number_of_block;
    default:
        RT_ASSERT("Not Support disk device" == 0);
    }
}

static size_t rt_disk_write(rt_device_t disk_dev, off_t block_off, size_t number_of_block, const void * buffer)
{
    size_t size;
    switch (disk_dev->type)
    {
    case RT_Device_Class_MTD:
        size = number_of_block * RT_MTD_NOR_DEVICE(disk_dev)->block_size;
        if(number_of_block != rt_disk_erase(disk_dev, block_off, number_of_block))
        {
            return 0;
        }
        if(size != rt_mtd_nor_write(RT_MTD_NOR_DEVICE(disk_dev), block_off * RT_MTD_NOR_DEVICE(disk_dev)->block_size, buffer, size))
        {
            return 0;
        }
        return number_of_block;
    case RT_Device_Class_Block:
        return rt_device_write(disk_dev, block_off, buffer, number_of_block);
    default:
        RT_ASSERT("Not Support disk device" == 0);
    }
}

static size_t rt_disk_read(rt_device_t disk_dev, off_t block_off, size_t number_of_block, const void * buffer)
{
    size_t size;
    switch (disk_dev->type)
    {
    case RT_Device_Class_MTD:
        size = number_of_block * RT_MTD_NOR_DEVICE(disk_dev)->block_size;
        if(size != rt_mtd_nor_read(RT_MTD_NOR_DEVICE(disk_dev), block_off * RT_MTD_NOR_DEVICE(disk_dev)->block_size, buffer, size))
        {
            return 0;
        }
        return number_of_block;
    case RT_Device_Class_Block:
        return rt_device_read(disk_dev, block_off, buffer, number_of_block);
    default:
        RT_ASSERT("Not Support disk device" == 0);
    }
}



VOID  rt_fx_disk_driver(FX_MEDIA *media_ptr)
{
    rt_device_t disk_dev = media_ptr->fx_media_driver_info;
    RT_ASSERT(media_ptr != RT_NULL);
    RT_ASSERT(disk_dev != RT_NULL);
    RT_ASSERT(disk_dev->type == RT_Device_Class_MTD || disk_dev->type == RT_Device_Class_Block);

    /* There are several useful/important pieces of information contained in 
       the media structure, some of which are supplied by FileX and others 
       are for the driver to setup. The following is a summary of the 
       necessary FX_MEDIA structure members:

            FX_MEDIA Member                    Meaning

        fx_media_driver_request             FileX request type. Valid requests from 
                                            FileX are as follows:

                                                    FX_DRIVER_READ
                                                    FX_DRIVER_WRITE
                                                    FX_DRIVER_FLUSH
                                                    FX_DRIVER_ABORT
                                                    FX_DRIVER_INIT
                                                    FX_DRIVER_BOOT_READ
                                                    FX_DRIVER_RELEASE_SECTORS
                                                    FX_DRIVER_BOOT_WRITE
                                                    FX_DRIVER_UNINIT

        fx_media_driver_status              This value is RETURNED by the driver. 
                                            If the operation is successful, this 
                                            field should be set to FX_SUCCESS for 
                                            before returning. Otherwise, if an 
                                            error occurred, this field should be 
                                            set to FX_IO_ERROR.

        fx_media_driver_buffer              Pointer to buffer to read or write 
                                            sector data. This is supplied by 
                                            FileX.

        fx_media_driver_logical_sector      Logical sector FileX is requesting.

        fx_media_driver_sectors             Number of sectors FileX is requesting.


       The following is a summary of the optional FX_MEDIA structure members:

            FX_MEDIA Member                              Meaning

        fx_media_driver_info                Pointer to any additional information 
                                            or memory. This is optional for the 
                                            driver use and is setup from the 
                                            fx_media_open call. The RAM disk uses
                                            this pointer for the RAM disk memory 
                                            itself.

        fx_media_driver_write_protect       The DRIVER sets this to FX_TRUE when 
                                            media is write protected. This is 
                                            typically done in initialization,
                                            but can be done anytime.

        fx_media_driver_free_sector_update  The DRIVER sets this to FX_TRUE when 
                                            it needs to know when clusters are 
                                            released. This is important for FLASH 
                                            wear-leveling drivers.

        fx_media_driver_system_write        FileX sets this flag to FX_TRUE if the 
                                            sector being written is a system sector, 
                                            e.g., a boot, FAT, or directory sector. 
                                            The driver may choose to use this to 
                                            initiate error recovery logic for greater
                                            fault tolerance.

        fx_media_driver_data_sector_read    FileX sets this flag to FX_TRUE if the 
                                            sector(s) being read are file data sectors, 
                                            i.e., NOT system sectors.

        fx_media_driver_sector_type         FileX sets this variable to the specific 
                                            type of sector being read or written. The 
                                            following sector types are identified:

                                                    FX_UNKNOWN_SECTOR
                                                    FX_BOOT_SECTOR
                                                    FX_FAT_SECTOR
                                                    FX_DIRECTORY_SECTOR
                                                    FX_DATA_SECTOR
     */

    /* Process the driver request specified in the media control block.  */
    switch (media_ptr -> fx_media_driver_request)
    {

    case FX_DRIVER_READ:
    {
        media_ptr -> fx_media_driver_status =  FX_SUCCESS;
        if(rt_disk_read(disk_dev, media_ptr -> fx_media_driver_logical_sector + media_ptr -> fx_media_hidden_sectors, media_ptr->fx_media_driver_buffer, media_ptr->fx_media_driver_sectors) != media_ptr->fx_media_driver_sectors)
        {
            media_ptr -> fx_media_driver_status = FX_IO_ERROR;
        }
        
        break;
    }

    case FX_DRIVER_WRITE:
    {
        media_ptr -> fx_media_driver_status =  FX_SUCCESS;
        if(rt_disk_write(disk_dev, media_ptr -> fx_media_driver_logical_sector + media_ptr -> fx_media_hidden_sectors, media_ptr->fx_media_driver_buffer, media_ptr->fx_media_driver_sectors) != media_ptr->fx_media_driver_sectors)
        {
            media_ptr -> fx_media_driver_status = FX_IO_ERROR;
        }
        break;
    }

    case FX_DRIVER_FLUSH:
    {

        /* Return driver success.  */
        media_ptr -> fx_media_driver_status =  FX_SUCCESS;
        break;
    }

    case FX_DRIVER_ABORT:
    {

        /* Return driver success.  */
        media_ptr -> fx_media_driver_status =  FX_SUCCESS;
        break;
    }

    case FX_DRIVER_INIT:
    {

        /* FLASH drivers are responsible for setting several fields in the
           media structure, as follows:

                media_ptr -> fx_media_driver_free_sector_update
                media_ptr -> fx_media_driver_write_protect

           The fx_media_driver_free_sector_update flag is used to instruct
           FileX to inform the driver whenever sectors are not being used.
           This is especially useful for FLASH managers so they don't have
           maintain mapping for sectors no longer in use.

           The fx_media_driver_write_protect flag can be set anytime by the
           driver to indicate the media is not writable.  Write attempts made
           when this flag is set are returned as errors.  */

        /* Perform basic initialization here... since the boot record is going
           to be read subsequently and again for volume name requests.  */

        /* Successful driver request.  */
        media_ptr -> fx_media_driver_status =  FX_SUCCESS;
        if(rt_device_open(disk_dev, RT_DEVICE_FLAG_RDWR) != RT_EOK)
        {
            media_ptr -> fx_media_driver_status =  FX_IO_ERROR;
        }
        break;
    }

    case FX_DRIVER_UNINIT:
    {

        /* There is nothing to do in this case for the RAM driver.  For actual
           devices some shutdown processing may be necessary.  */

        /* Successful driver request.  */
        media_ptr -> fx_media_driver_status =  FX_SUCCESS;
        if(rt_device_close(disk_dev) != RT_EOK)
        {
            media_ptr -> fx_media_driver_status =  FX_IO_ERROR;
        }
        break;
    }

    case FX_DRIVER_BOOT_READ:
    {

        /* Read the boot record and return to the caller.  */

        media_ptr -> fx_media_driver_status =  FX_SUCCESS;
        if(rt_disk_read(disk_dev, 0, media_ptr->fx_media_driver_buffer, media_ptr->fx_media_driver_sectors) != media_ptr->fx_media_driver_sectors)
        {
            media_ptr -> fx_media_driver_status = FX_IO_ERROR;
        }
        break;
    }

    case FX_DRIVER_BOOT_WRITE:
    {

        media_ptr -> fx_media_driver_status =  FX_SUCCESS;
        if(rt_disk_write(disk_dev, 0, media_ptr->fx_media_driver_buffer, media_ptr->fx_media_driver_sectors) != media_ptr->fx_media_driver_sectors)
        {
            media_ptr -> fx_media_driver_status = FX_IO_ERROR;
        }
        break;
    }

    default:
    {

        /* Invalid driver request.  */
        media_ptr -> fx_media_driver_status =  FX_IO_ERROR;
        break;
    }
    }
}