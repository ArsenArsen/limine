#include <stdint.h>
#include <stddef.h>
#include <drivers/disk.h>
#include <lib/libc.h>
#if bios == 1
#  include <lib/real.h>
#elif uefi == 1
#  include <efi.h>
#endif
#include <lib/blib.h>
#include <lib/print.h>
#include <mm/pmm.h>
#include <sys/cpu.h>

#define MAX_VOLUMES 64

#if bios == 1

struct bios_drive_params {
    uint16_t buf_size;
    uint16_t info_flags;
    uint32_t cyl;
    uint32_t heads;
    uint32_t sects;
    uint64_t lba_count;
    uint16_t bytes_per_sect;
    uint32_t edd;
} __attribute__((packed));

struct dap {
    uint16_t size;
    uint16_t count;
    uint16_t offset;
    uint16_t segment;
    uint64_t lba;
};

#define XFER_BUF_SIZE (xfer_sizes[SIZEOF_ARRAY(xfer_sizes) - 1] * 512)
static const size_t xfer_sizes[] = { 1, 2, 4, 8, 16, 24, 32, 48, 64 };
static void *xfer_buf = NULL;

static size_t fastest_xfer_size(struct volume *volume) {
    struct dap dap = {0};

    if (xfer_buf == NULL)
        xfer_buf = conv_mem_alloc(XFER_BUF_SIZE);

    size_t fastest_size = 1;
    uint64_t last_speed = (uint64_t)-1;

    for (size_t i = 0; i < SIZEOF_ARRAY(xfer_sizes); i++) {
        if (xfer_sizes[i] * volume->sector_size > XFER_BUF_SIZE) {
            break;
        }

        dap.size    = 16;
        dap.count   = xfer_sizes[i];
        dap.segment = rm_seg(xfer_buf);
        dap.offset  = rm_off(xfer_buf);
        dap.lba     = 0;

        struct rm_regs r = {0};
        r.eax = 0x4200;
        r.edx = volume->drive;
        r.esi = (uint32_t)rm_off(&dap);
        r.ds  = rm_seg(&dap);

        uint64_t start_timestamp = rdtsc();
        rm_int(0x13, &r, &r);
        uint64_t end_timestamp = rdtsc();

        if (r.eflags & EFLAGS_CF) {
            int ah = (r.eax >> 8) & 0xff;
            printv("Disk error %x. Drive %x", ah, volume->drive);
            continue;
        }

        uint64_t speed = (end_timestamp - start_timestamp) / xfer_sizes[i];

        if (speed < last_speed) {
            last_speed = speed;
            fastest_size = xfer_sizes[i];
        }
    }

    return fastest_size;
}

bool disk_read_sectors(struct volume *volume, void *buf, uint64_t block, size_t count) {
    struct dap dap = {0};

    if (count * volume->sector_size > XFER_BUF_SIZE)
        panic("XFER");

    if (xfer_buf == NULL)
        xfer_buf = conv_mem_alloc(XFER_BUF_SIZE);

    dap.size    = 16;
    dap.count   = count;
    dap.segment = rm_seg(xfer_buf);
    dap.offset  = rm_off(xfer_buf);
    dap.lba     = block;

    struct rm_regs r = {0};
    r.eax = 0x4200;
    r.edx = volume->drive;
    r.esi = (uint32_t)rm_off(&dap);
    r.ds  = rm_seg(&dap);

    rm_int(0x13, &r, &r);

    if (r.eflags & EFLAGS_CF) {
        int ah = (r.eax >> 8) & 0xff;
        switch (ah) {
            case 0x0c:
                return false;
            default:
                panic("Disk error %x. Drive %x, LBA %x.",
                      ah, volume->drive, dap.lba);
        }
    }

    if (buf != NULL)
        memcpy(buf, xfer_buf, count * volume->sector_size);

    return true;
}

void disk_create_index(void) {
    volume_index = ext_mem_alloc(sizeof(struct volume) * MAX_VOLUMES);

    int optical_indices = 1, hdd_indices = 1;

    for (uint8_t drive = 0x80; drive < 0xf0; drive++) {
        if (volume_index_i == MAX_VOLUMES) {
            print("WARNING: TOO MANY VOLUMES!");
            break;
        }

        struct rm_regs r = {0};
        struct bios_drive_params drive_params;

        r.eax = 0x4800;
        r.edx = drive;
        r.ds  = rm_seg(&drive_params);
        r.esi = rm_off(&drive_params);

        drive_params.buf_size = sizeof(struct bios_drive_params);

        rm_int(0x13, &r, &r);

        if (r.eflags & EFLAGS_CF)
            continue;

        if (drive_params.lba_count == 0 || drive_params.bytes_per_sect == 0)
            continue;

        struct volume *block = ext_mem_alloc(sizeof(struct volume));

        block->drive = drive;
        block->partition = 0;
        block->sector_size = drive_params.bytes_per_sect;
        block->first_sect = 0;
        block->sect_count = drive_params.lba_count;
        block->max_partition = -1;

        if (drive_params.info_flags & (1 << 2) && drive > 0x8f) {
            // The medium could not be present (e.g.: CD-ROMs)
            // Do a test run to see if we can actually read it
            if (!disk_read_sectors(block, NULL, 0, 1)) {
                continue;
            }
            block->index = optical_indices++;
            block->is_optical = true;
        } else {
            block->index = hdd_indices++;
        }

        block->fastest_xfer_size = fastest_xfer_size(block);

        if (gpt_get_guid(&block->guid, block)) {
            block->guid_valid = true;
        }

        volume_index[volume_index_i++] = block;

        for (int part = 0; ; part++) {
            struct volume *p = ext_mem_alloc(sizeof(struct volume));
            int ret = part_get(p, block, part);

            if (ret == END_OF_TABLE || ret == INVALID_TABLE)
                break;
            if (ret == NO_PARTITION)
                continue;

            volume_index[volume_index_i++] = p;

            block->max_partition++;
        }
    }
}

#endif

#if uefi == 1

struct volume *disk_volume_from_efi_handle(EFI_HANDLE *efi_handle) {
    EFI_STATUS status;

    struct volume *ret = NULL;

    EFI_GUID disk_io_guid = DISK_IO_PROTOCOL;
    EFI_GUID block_io_guid = BLOCK_IO_PROTOCOL;
    EFI_DISK_IO *disk_io = NULL;
    EFI_BLOCK_IO *block_io = NULL;

    status = gBS->HandleProtocol(efi_handle, &disk_io_guid, (void **)&disk_io);
    if (status)
        return NULL;
    status = gBS->HandleProtocol(efi_handle, &block_io_guid, (void **)&block_io);
    if (status)
        return NULL;

    uint64_t signature = BUILD_ID;
    uint64_t orig;

    disk_io->ReadDisk(disk_io, block_io->Media->MediaId, 0, sizeof(uint64_t), &orig);

    status = disk_io->WriteDisk(disk_io, block_io->Media->MediaId, 0, sizeof(uint64_t), &signature);

    if (status) {
        // Really hacky support for CDs because they are read-only
        for (size_t i = 0; i < volume_index_i; i++) {
            if (volume_index[i]->is_optical)
                return volume_index[i];
        }

        return NULL;
    }

    for (size_t i = 0; i < volume_index_i; i++) {
        uint64_t compare;

        EFI_DISK_IO *cur_disk_io = NULL;
        EFI_BLOCK_IO *cur_block_io = NULL;

        gBS->HandleProtocol(volume_index[i]->efi_handle,
                          &disk_io_guid, (void **)&cur_disk_io);
        gBS->HandleProtocol(volume_index[i]->efi_handle,
                          &block_io_guid, (void **)&cur_block_io);

        cur_disk_io->ReadDisk(cur_disk_io,
                          cur_block_io->Media->MediaId,
                          0 +
                          volume_index[i]->first_sect * volume_index[i]->sector_size,
                          sizeof(uint64_t), &compare);

        if (compare == signature) {
            ret = volume_index[i];
            break;
        }
    }

    disk_io->WriteDisk(disk_io, block_io->Media->MediaId, 0, sizeof(uint64_t), &orig);

    return ret;
}

bool disk_read_sectors(struct volume *volume, void *buf, uint64_t block, size_t count) {
    EFI_STATUS status;

    EFI_GUID block_io_guid = BLOCK_IO_PROTOCOL;
    EFI_BLOCK_IO *block_io = NULL;

    status = gBS->HandleProtocol(volume->efi_handle,
                               &block_io_guid, (void **)&block_io);

    status = block_io->ReadBlocks(block_io,
                               block_io->Media->MediaId,
                               block, count * volume->sector_size, buf);

    if (status != 0) {
        return false;
    }

    return true;
}

void disk_create_index(void) {
    EFI_STATUS status;

    EFI_GUID block_io_guid = BLOCK_IO_PROTOCOL;
    EFI_HANDLE *handles = NULL;
    UINTN handles_size = 0;

    gBS->LocateHandle(ByProtocol, &block_io_guid, NULL, &handles_size, handles);

    handles = ext_mem_alloc(handles_size);

    gBS->LocateHandle(ByProtocol, &block_io_guid, NULL, &handles_size, handles);

    volume_index = ext_mem_alloc(sizeof(struct volume) * MAX_VOLUMES);

    int optical_indices = 1, hdd_indices = 1;

    for (size_t i = 0; i < handles_size / sizeof(EFI_HANDLE); i++) {
        if (volume_index_i == MAX_VOLUMES) {
            print("WARNING: TOO MANY VOLUMES!");
            break;
        }

        EFI_GUID disk_io_guid = DISK_IO_PROTOCOL;
        EFI_DISK_IO *disk_io = NULL;

        gBS->HandleProtocol(handles[i], &disk_io_guid, (void **)&disk_io);

        EFI_BLOCK_IO *drive = NULL;

        status = gBS->HandleProtocol(handles[i], &block_io_guid, (void **)&drive);

        if (status != 0 || drive == NULL || drive->Media->LastBlock == 0)
            continue;

        if (drive->Media->LogicalPartition)
            continue;

        uint64_t orig;
        disk_io->ReadDisk(disk_io, drive->Media->MediaId, 0, sizeof(uint64_t), &orig);
        status = disk_io->WriteDisk(disk_io, drive->Media->MediaId, 0, sizeof(uint64_t), &orig);

        struct volume *block = ext_mem_alloc(sizeof(struct volume));

        if (status) {
            block->index = optical_indices++;
            block->is_optical = true;
        } else {
            block->index = hdd_indices++;
        }

        block->efi_handle = handles[i];
        block->partition = 0;
        block->sector_size = drive->Media->BlockSize;
        block->first_sect = 0;
        block->sect_count = drive->Media->LastBlock + 1;
        block->max_partition = -1;

        // TODO: get fastest xfer size also for UEFI?
        block->fastest_xfer_size = 8;

        if (gpt_get_guid(&block->guid, block)) {
            block->guid_valid = true;
        }

        volume_index[volume_index_i++] = block;

        for (int part = 0; ; part++) {
            struct volume *p = ext_mem_alloc(sizeof(struct volume));
            int ret = part_get(p, block, part);

            if (ret == END_OF_TABLE || ret == INVALID_TABLE)
                break;
            if (ret == NO_PARTITION)
                continue;

            volume_index[volume_index_i++] = p;

            block->max_partition++;
        }
    }
}

#endif
