/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC - common functions */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "otp.h"
#include "rwsig.h"
#include "shared_mem.h"
#include "system.h"
#include "util.h"
#include "vboot_hash.h"
#include "softwareWatchdog.h"

/*
 * Contents of erased flash, as a 32-bit value.  Most platforms erase flash
 * bits to 1.
 */
#ifndef CONFIG_FLASH_ERASED_VALUE32
#define CONFIG_FLASH_ERASED_VALUE32 (-1U)
#endif

#ifdef CONFIG_FLASH_PSTATE

/*
 * If flash isn't mapped to the EC's address space, it's probably SPI, and
 * should be using SPI write protect, not PSTATE.
 */
#if !defined(CONFIG_INTERNAL_STORAGE) || !defined(CONFIG_MAPPED_STORAGE)
#error "PSTATE should only be used with internal mem-mapped flash."
#endif

#ifdef CONFIG_FLASH_PSTATE_BANK
/* Persistent protection state - emulates a SPI status register for flashrom */
/* NOTE: It's not expected that RO and RW will support
 * differing PSTATE versions. */
#define PERSIST_STATE_VERSION 3  /* Expected persist_state.version */

/* Flags for persist_state.flags */
/* Protect persist state and RO firmware at boot */
#define PERSIST_FLAG_PROTECT_RO 0x02
#define PSTATE_VALID_FLAGS      BIT(0)
#define PSTATE_VALID_SERIALNO   BIT(1)
#define PSTATE_VALID_MAC_ADDR   BIT(2)

struct persist_state {
	uint8_t version;            /* Version of this struct */
	uint8_t flags;              /* Lock flags (PERSIST_FLAG_*) */
	uint8_t valid_fields;       /* Flags for valid data. */
	uint8_t reserved;           /* Reserved; set 0 */
#ifdef CONFIG_SERIALNO_LEN
	uint8_t serialno[CONFIG_SERIALNO_LEN]; /* Serial number. */
#endif /* CONFIG_SERIALNO_LEN */
#ifdef CONFIG_MAC_ADDR_LEN
	uint8_t mac_addr[CONFIG_MAC_ADDR_LEN];
#endif /* CONFIG_MAC_ADDR_LEN */
#if !defined(CONFIG_SERIALNO_LEN) && !defined(CONFIG_MAC_ADDR_LEN)
	uint8_t padding[4 % CONFIG_FLASH_WRITE_SIZE];
#endif
};

/* written with flash_physical_write, need to respect alignment constraints */
#ifndef CHIP_FAMILY_STM32L /* STM32L1xx is somewhat lying to us */
BUILD_ASSERT(sizeof(struct persist_state) % CONFIG_FLASH_WRITE_SIZE == 0);
#endif

BUILD_ASSERT(sizeof(struct persist_state) <= CONFIG_FW_PSTATE_SIZE);

#else /* !CONFIG_FLASH_PSTATE_BANK */

/*
 * Flags for write protect state depend on the erased value of flash.  The
 * locked value must be the same as the unlocked value with one or more bits
 * transitioned away from the erased state.  That way, it is possible to
 * rewrite the data in-place to set the lock.
 *
 * STM32F0x can only write 0x0000 to a non-erased half-word, which means
 * PSTATE_MAGIC_LOCKED isn't quite as pretty.  That's ok; the only thing
 * we actually need to detect is PSTATE_MAGIC_UNLOCKED, since that's the
 * only value we'll ever alter, and the only value which causes us not to
 * lock the flash at boot.
 */
#if (CONFIG_FLASH_ERASED_VALUE32 == -1U)
#define PSTATE_MAGIC_UNLOCKED 0x4f4e5057  /* "WPNO" */
#define PSTATE_MAGIC_LOCKED   0x00000000  /* ""     */
#elif (CONFIG_FLASH_ERASED_VALUE32 == 0)
#define PSTATE_MAGIC_UNLOCKED 0x4f4e5057  /* "WPNO" */
#define PSTATE_MAGIC_LOCKED   0x5f5f5057  /* "WP__" */
#else
/* What kind of wacky flash doesn't erase all bits to 1 or 0? */
#error "PSTATE needs magic values for this flash architecture."
#endif

/*
 * Rewriting the write protect flag in place currently requires a minimum write
 * size <= the size of the flag value.
 *
 * We could work around this on chips with larger minimum write size by reading
 * the write block containing the flag into RAM, changing it to the locked
 * value, and then rewriting that block.  But we should only pay for that
 * complexity when we run across another chip which needs it.
 */
#if (CONFIG_FLASH_WRITE_SIZE > 4)
#error "Non-bank-based PSTATE requires flash write size <= 32 bits."
#endif

const uint32_t pstate_data __attribute__((section(".rodata.pstate"))) =
#ifdef CONFIG_FLASH_PSTATE_LOCKED
	PSTATE_MAGIC_LOCKED;
#else
	PSTATE_MAGIC_UNLOCKED;
#endif

#endif /* !CONFIG_FLASH_PSTATE_BANK */
#endif /* CONFIG_FLASH_PSTATE */

#ifdef CONFIG_FLASH_MULTIPLE_REGION
const struct ec_flash_bank *flash_bank_info(int bank)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(flash_bank_array); i++) {
		if (bank < flash_bank_array[i].count)
			return &flash_bank_array[i];
		bank -= flash_bank_array[i].count;
	}

	return NULL;
}

int flash_bank_size(int bank)
{
	int rv;
	const struct ec_flash_bank *info = flash_bank_info(bank);

	if (!info)
		return -1;

	rv = BIT(info->size_exp);
	ASSERT(rv > 0);
	return rv;
}

int flash_bank_erase_size(int bank)
{
	int rv;
	const struct ec_flash_bank *info = flash_bank_info(bank);

	if (!info)
		return -1;

	rv = BIT(info->erase_size_exp);
	ASSERT(rv > 0);
	return rv;
}

int flash_bank_index(int offset)
{
	int bank_offset = 0, i;

	if (offset == 0)
		return bank_offset;

	for (i = 0; i < ARRAY_SIZE(flash_bank_array); i++) {
		int all_sector_size = flash_bank_array[i].count <<
			flash_bank_array[i].size_exp;
		if (offset >= all_sector_size) {
			offset -= all_sector_size;
			bank_offset += flash_bank_array[i].count;
			continue;
		}
		if (offset & ((1 << flash_bank_array[i].size_exp) - 1))
			return -1;
		return bank_offset + (offset >> flash_bank_array[i].size_exp);
	}
	if (offset != 0)
		return -1;
	return bank_offset;
}

int flash_bank_count(int offset, int size)
{
	int begin = flash_bank_index(offset);
	int end = flash_bank_index(offset + size);

	if (begin == -1 || end == -1)
		return -1;
	return end - begin;
}

int flash_bank_start_offset(int bank)
{
	int i;
	int offset;
	int bank_size;

	if (bank < 0)
		return -1;

	offset = 0;
	for (i = 0; i < bank; i++) {
		bank_size = flash_bank_size(i);
		if (bank_size < 0)
			return -1;
		offset += bank_size;
	}

	return offset;
}

#endif  /* CONFIG_FLASH_MULTIPLE_REGION */

static int flash_range_ok(int offset, int size_req, int align)
{
	if (offset < 0 || size_req < 0 ||
	    offset > CONFIG_FLASH_SIZE ||
	    size_req > CONFIG_FLASH_SIZE ||
	    offset + size_req > CONFIG_FLASH_SIZE ||
	    (offset | size_req) & (align - 1))
		return 0;  /* Invalid range */

	return 1;
}

#ifdef CONFIG_MAPPED_STORAGE
/**
 * Get the physical memory address of a flash offset
 *
 * This is used for direct flash access. We assume that the flash is
 * contiguous from this start address through to the end of the usable
 * flash.
 *
 * @param offset	Flash offset to get address of
 * @param dataptrp	Returns pointer to memory address of flash offset
 * @return pointer to flash memory offset, if ok, else NULL
 */
static const char *flash_physical_dataptr(int offset)
{
	return (char *)((uintptr_t)CONFIG_MAPPED_STORAGE_BASE + offset);
}

int flash_dataptr(int offset, int size_req, int align, const char **ptrp)
{
	if (!flash_range_ok(offset, size_req, align))
		return -1;  /* Invalid range */
	if (ptrp)
		*ptrp = flash_physical_dataptr(offset);

	return CONFIG_FLASH_SIZE - offset;
}
#endif

#ifdef CONFIG_FLASH_PSTATE
#ifdef CONFIG_FLASH_PSTATE_BANK

/**
 * Read and return persistent state flags (EC_FLASH_PROTECT_*)
 */
static uint32_t flash_read_pstate(void)
{
	const struct persist_state *pstate =
		(const struct persist_state *)
		flash_physical_dataptr(CONFIG_FW_PSTATE_OFF);

	if ((pstate->version == PERSIST_STATE_VERSION) &&
	    (pstate->valid_fields & PSTATE_VALID_FLAGS) &&
	    (pstate->flags & PERSIST_FLAG_PROTECT_RO)) {
		/* Lock flag is known to be set */
		return EC_FLASH_PROTECT_RO_AT_BOOT;
	} else {
#ifdef CONFIG_WP_ALWAYS
		return PERSIST_FLAG_PROTECT_RO;
#else
		return 0;
#endif
	}
}

/**
 * Write persistent state after erasing.
 *
 * @param pstate	New data to set in pstate. NOT memory mapped
 *                      old pstate as it will be erased.
 * @return EC_SUCCESS, or nonzero if error.
 */
static int flash_write_pstate_data(struct persist_state *newpstate)
{
	int rv;

	/* Erase pstate */
	rv = flash_physical_erase(CONFIG_FW_PSTATE_OFF,
				  CONFIG_FW_PSTATE_SIZE);
	if (rv)
		return rv;

	/*
	 * Note that if we lose power in here, we'll lose the pstate contents.
	 * That's ok, because it's only possible to write the pstate before
	 * it's protected.
	 */

	/* Write the updated pstate */
	return flash_physical_write(CONFIG_FW_PSTATE_OFF, sizeof(*newpstate),
				    (const char *)newpstate);
}



/**
 * Validate and Init persistent state datastructure.
 *
 * @param pstate	A pstate data structure. Will be valid at complete.
 * @return EC_SUCCESS, or nonzero if error.
 */
static int validate_pstate_struct(struct persist_state *pstate)
{
	if (pstate->version != PERSIST_STATE_VERSION) {
		memset(pstate, 0, sizeof(*pstate));
		pstate->version = PERSIST_STATE_VERSION;
	}

	return EC_SUCCESS;
}

/**
 * Write persistent state from pstate, erasing if necessary.
 *
 * @param flags		New flash write protect flags to set in pstate.
 * @return EC_SUCCESS, or nonzero if error.
 */
static int flash_write_pstate(uint32_t flags)
{
	struct persist_state newpstate;
	const struct persist_state *pstate =
		(const struct persist_state *)
		flash_physical_dataptr(CONFIG_FW_PSTATE_OFF);

	/* Only check the flags we write to pstate */
	flags &= EC_FLASH_PROTECT_RO_AT_BOOT;

	/* Check if pstate has actually changed */
	if (flags == flash_read_pstate())
		return EC_SUCCESS;

	/* Cache the old copy for read/modify/write. */
	memcpy(&newpstate, pstate, sizeof(newpstate));
	validate_pstate_struct(&newpstate);

	if (flags & EC_FLASH_PROTECT_RO_AT_BOOT)
		newpstate.flags |= PERSIST_FLAG_PROTECT_RO;
	else
		newpstate.flags &= ~PERSIST_FLAG_PROTECT_RO;
	newpstate.valid_fields |= PSTATE_VALID_FLAGS;

	return flash_write_pstate_data(&newpstate);
}

#ifdef CONFIG_SERIALNO_LEN
/**
 * Read and return persistent serial number.
 */
const char *flash_read_pstate_serial(void)
{
	const struct persist_state *pstate =
		(const struct persist_state *)
		flash_physical_dataptr(CONFIG_FW_PSTATE_OFF);

	if ((pstate->version == PERSIST_STATE_VERSION) &&
	    (pstate->valid_fields & PSTATE_VALID_SERIALNO)) {
		return (const char *)(pstate->serialno);
	}

	return NULL;
}

/**
 * Write persistent serial number to pstate, erasing if necessary.
 *
 * @param serialno		New ascii serial number to set in pstate.
 * @return EC_SUCCESS, or nonzero if error.
 */
int flash_write_pstate_serial(const char *serialno)
{
	int length;
	struct persist_state newpstate;
	const struct persist_state *pstate =
		(const struct persist_state *)
		flash_physical_dataptr(CONFIG_FW_PSTATE_OFF);

	/* Check that this is OK */
	if (!serialno)
		return EC_ERROR_INVAL;

	length = strnlen(serialno, sizeof(newpstate.serialno));
	if (length >= sizeof(newpstate.serialno)) {
		return EC_ERROR_INVAL;
	}

	/* Cache the old copy for read/modify/write. */
	memcpy(&newpstate, pstate, sizeof(newpstate));
	validate_pstate_struct(&newpstate);

	/*
	 * Erase any prior data and copy the string. The length was verified to
	 * be shorter than the buffer so a null terminator always remains.
	 */
	memset(newpstate.serialno, '\0', sizeof(newpstate.serialno));
	memcpy(newpstate.serialno, serialno, length);

	newpstate.valid_fields |= PSTATE_VALID_SERIALNO;

	return flash_write_pstate_data(&newpstate);
}

#endif /* CONFIG_SERIALNO_LEN */

#ifdef CONFIG_MAC_ADDR_LEN

/**
 * Read and return persistent MAC address.
 */
const char *flash_read_pstate_mac_addr(void)
{
	const struct persist_state *pstate =
		(const struct persist_state *)
		flash_physical_dataptr(CONFIG_FW_PSTATE_OFF);

	if ((pstate->version == PERSIST_STATE_VERSION) &&
	    (pstate->valid_fields & PSTATE_VALID_MAC_ADDR)) {
		return (const char *)(pstate->mac_addr);
	}

	return NULL;
}

/**
 * Write persistent MAC Addr to pstate, erasing if necessary.
 *
 * @param mac_addr		New ascii MAC address to set in pstate.
 * @return EC_SUCCESS, or nonzero if error.
 */
int flash_write_pstate_mac_addr(const char *mac_addr)
{
	int length;
	struct persist_state newpstate;
	const struct persist_state *pstate =
		(const struct persist_state *)
		flash_physical_dataptr(CONFIG_FW_PSTATE_OFF);

	/* Check that this is OK, data is valid and fits in the region. */
	if (!mac_addr) {
		return EC_ERROR_INVAL;
	}

	/*
	 * This will perform validation of the mac address before storing it.
	 * The MAC address format is '12:34:56:78:90:AB', a 17 character long
	 * string containing pairs of hex digits, each pair delimited by a ':'.
	 */
	length = strnlen(mac_addr, sizeof(newpstate.mac_addr));
	if (length != 17) {
		return EC_ERROR_INVAL;
	}
	for (int i = 0; i < 17; i++) {
		if (i % 3 != 2) {
			/* Verify the remaining characters are hex digits. */
			if ((mac_addr[i] < '0' || '9' < mac_addr[i]) &&
			    (mac_addr[i] < 'A' || 'F' < mac_addr[i]) &&
			    (mac_addr[i] < 'a' || 'f' < mac_addr[i])) {
				return EC_ERROR_INVAL;
			}
		} else {
			/* Every 3rd character is a ':' */
			if (mac_addr[i] != ':') {
				return EC_ERROR_INVAL;
			}
		}
	}

	/* Cache the old copy for read/modify/write. */
	memcpy(&newpstate, pstate, sizeof(newpstate));
	validate_pstate_struct(&newpstate);

	/*
	 * Erase any prior data and copy the string. The length was verified to
	 * be shorter than the buffer so a null terminator always remains.
	 */
	memset(newpstate.mac_addr, '\0', sizeof(newpstate.mac_addr));
	memcpy(newpstate.mac_addr, mac_addr, length);

	newpstate.valid_fields |= PSTATE_VALID_MAC_ADDR;

	return flash_write_pstate_data(&newpstate);
}

#endif /* CONFIG_MAC_ADDR_LEN */

#else /* !CONFIG_FLASH_PSTATE_BANK */

/**
 * Return the address of the pstate data in EC-RO.
 */
static const uintptr_t get_pstate_addr(void)
{
	uintptr_t addr = (uintptr_t)&pstate_data;

	/* Always use the pstate data in RO, even if we're RW */
	if (system_is_in_rw())
		addr += CONFIG_RO_MEM_OFF - CONFIG_RW_MEM_OFF;

	return addr;
}

/**
 * Read and return persistent state flags (EC_FLASH_PROTECT_*)
 */
static uint32_t flash_read_pstate(void)
{
	/* Check for the unlocked magic value */
	if (*(const uint32_t *)get_pstate_addr() == PSTATE_MAGIC_UNLOCKED)
		return 0;

	/* Anything else is locked */
	return EC_FLASH_PROTECT_RO_AT_BOOT;
}

/**
 * Write persistent state from pstate, erasing if necessary.
 *
 * @param flags		New flash write protect flags to set in pstate.
 * @return EC_SUCCESS, or nonzero if error.
 */
static int flash_write_pstate(uint32_t flags)
{
	const uint32_t new_pstate = PSTATE_MAGIC_LOCKED;

	/* Only check the flags we write to pstate */
	flags &= EC_FLASH_PROTECT_RO_AT_BOOT;

	/* Check if pstate has actually changed */
	if (flags == flash_read_pstate())
		return EC_SUCCESS;

	/* We can only set the protect flag, not clear it */
	if (!(flags & EC_FLASH_PROTECT_RO_AT_BOOT))
		return EC_ERROR_ACCESS_DENIED;

	/*
	 * Write a new pstate.  We can overwrite the existing value, because
	 * we're only moving bits from the erased state to the unerased state.
	 */
	return flash_physical_write(get_pstate_addr() -
				    CONFIG_PROGRAM_MEMORY_BASE,
				    sizeof(new_pstate),
				    (const char *)&new_pstate);
}

#endif /* !CONFIG_FLASH_PSTATE_BANK */
#endif /* CONFIG_FLASH_PSTATE */

int flash_is_erased(uint32_t offset, int size)
{
	const uint32_t *ptr;

#ifdef CONFIG_MAPPED_STORAGE
	/* Use pointer directly to flash */
	if (flash_dataptr(offset, size, sizeof(uint32_t),
			  (const char **)&ptr) < 0)
		return 0;

	flash_lock_mapped_storage(1);
	for (size /= sizeof(uint32_t); size > 0; size--, ptr++)
		if (*ptr != CONFIG_FLASH_ERASED_VALUE32) {
			flash_lock_mapped_storage(0);
			return 0;
	}

	flash_lock_mapped_storage(0);
#else
	/* Read flash a chunk at a time */
	uint32_t buf[8];
	int bsize;

	while (size) {
		bsize = MIN(size, sizeof(buf));

		if (flash_read(offset, bsize, (char *)buf))
			return 0;

		size -= bsize;
		offset += bsize;

		ptr = buf;
		for (bsize /= sizeof(uint32_t); bsize > 0; bsize--, ptr++)
			if (*ptr != CONFIG_FLASH_ERASED_VALUE32)
				return 0;

	}
#endif

	return 1;
}

int flash_read(int offset, int size, char *data)
{
#ifdef CONFIG_MAPPED_STORAGE
	const char *src;

	if (flash_dataptr(offset, size, 1, &src) < 0)
		return EC_ERROR_INVAL;

	flash_lock_mapped_storage(1);
	memcpy(data, src, size);
	flash_lock_mapped_storage(0);
	return EC_SUCCESS;
#else
	return flash_physical_read(offset, size, data);
#endif
}

static void flash_abort_or_invalidate_hash(int offset, int size)
{
#ifdef CONFIG_VBOOT_HASH
	if (vboot_hash_in_progress()) {
		/* Abort hash calculation when flash update is in progress. */
		vboot_hash_abort();
		return;
	}

#ifdef CONFIG_EXTERNAL_STORAGE
	/*
	 * If EC executes in RAM and is currently in RW, we keep the current
	 * hash. On the next hash check, AP will catch hash mismatch between the
	 * flash copy and the RAM copy, then take necessary actions.
	 */
	if (system_is_in_rw())
		return;
#endif

	/* If EC executes in place, we need to invalidate the cached hash. */
	vboot_hash_invalidate(offset, size);
#endif

#ifdef HAS_TASK_RWSIG
	/*
	 * If RW flash has been written to, make sure we do not automatically
	 * jump to RW after the timeout.
	 */
	if ((offset >= CONFIG_RW_MEM_OFF &&
		    offset < (CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE)) ||
	    ((offset + size) > CONFIG_RW_MEM_OFF &&
		    (offset + size) <= (CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE)) ||
	    (offset < CONFIG_RW_MEM_OFF &&
		    (offset + size) > (CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE)))
		rwsig_abort();
#endif
}

int flash_write(int offset, int size, const char *data)
{
	if (!flash_range_ok(offset, size, CONFIG_FLASH_WRITE_SIZE))
		return EC_ERROR_INVAL;  /* Invalid range */

	flash_abort_or_invalidate_hash(offset, size);

	return flash_physical_write(offset, size, data);
}

int flash_erase(int offset, int size)
{
#ifndef CONFIG_FLASH_MULTIPLE_REGION
	if (!flash_range_ok(offset, size, CONFIG_FLASH_ERASE_SIZE))
		return EC_ERROR_INVAL;  /* Invalid range */
#endif

	flash_abort_or_invalidate_hash(offset, size);

	return flash_physical_erase(offset, size);
}

int flash_protect_at_boot(uint32_t new_flags)
{
#ifdef CONFIG_FLASH_PSTATE
	uint32_t new_pstate_flags = new_flags & EC_FLASH_PROTECT_RO_AT_BOOT;

	/* Read the current persist state from flash */
	if (flash_read_pstate() != new_pstate_flags) {
		/* Need to update pstate */
		int rv;

#ifdef CONFIG_FLASH_PSTATE_BANK
		/* Fail if write protect block is already locked */
		if (flash_physical_get_protect(PSTATE_BANK))
			return EC_ERROR_ACCESS_DENIED;
#endif

		/* Write the desired flags */
		rv = flash_write_pstate(new_pstate_flags);
		if (rv)
			return rv;
	}

#ifdef CONFIG_FLASH_PROTECT_NEXT_BOOT
	/*
	 * Try updating at-boot protection state, if on a platform where write
	 * protection only changes after a reboot.  Otherwise we wouldn't
	 * update it until after the next reboot, and we'd need to reboot
	 * again.  Ignore errors, because the protection registers might
	 * already be locked this boot, and we'll still apply the correct state
	 * again on the next boot.
	 *
	 * This assumes PSTATE immediately follows RO, which it does on
	 * all STM32 platforms (which are the only ones with this config).
	 */
	flash_physical_protect_at_boot(new_flags);
#endif

	return EC_SUCCESS;
#else
	return flash_physical_protect_at_boot(new_flags);
#endif
}

uint32_t flash_get_protect(void)
{
	uint32_t flags = 0;
	int i;
	/* Region protection status */
	int not_protected[FLASH_REGION_COUNT] = {0};
#ifdef CONFIG_ROLLBACK
	/* Flags that must be set to set ALL_NOW flag. */
	const uint32_t all_flags = EC_FLASH_PROTECT_RO_NOW |
				   EC_FLASH_PROTECT_RW_NOW |
				   EC_FLASH_PROTECT_ROLLBACK_NOW;
#else
	const uint32_t all_flags = EC_FLASH_PROTECT_RO_NOW |
				   EC_FLASH_PROTECT_RW_NOW;
#endif

	/* Read write protect GPIO */
#ifdef CONFIG_WP_ALWAYS
	flags |= EC_FLASH_PROTECT_GPIO_ASSERTED;
#elif defined(CONFIG_WP_ACTIVE_HIGH)
	if (gpio_get_level(GPIO_WP))
		flags |= EC_FLASH_PROTECT_GPIO_ASSERTED;
#else
	if (!gpio_get_level(GPIO_WP_L))
		flags |= EC_FLASH_PROTECT_GPIO_ASSERTED;
#endif

#ifdef CONFIG_FLASH_PSTATE
	/* Read persistent state of RO-at-boot flag */
	flags |= flash_read_pstate();
#endif

	/* Scan flash protection */
	for (i = 0; i < PHYSICAL_BANKS; i++) {
		int is_ro = (i >= WP_BANK_OFFSET &&
			i < WP_BANK_OFFSET + WP_BANK_COUNT);
		enum flash_region region = is_ro ? FLASH_REGION_RO :
			FLASH_REGION_RW;
		int bank_flag = is_ro ? EC_FLASH_PROTECT_RO_NOW :
			EC_FLASH_PROTECT_RW_NOW;

#ifdef CONFIG_ROLLBACK
		if (i >= ROLLBACK_BANK_OFFSET &&
		    i < ROLLBACK_BANK_OFFSET + ROLLBACK_BANK_COUNT) {
			region = FLASH_REGION_ROLLBACK;
			bank_flag = EC_FLASH_PROTECT_ROLLBACK_NOW;
		}
#endif

		if (flash_physical_get_protect(i)) {
			/* At least one bank in the region is protected */
			flags |= bank_flag;
			if (not_protected[region])
				flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		} else {
			/* At least one bank in the region is NOT protected */
			not_protected[region] = 1;
			if (flags & bank_flag)
				flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		}
	}

	if ((flags & all_flags) == all_flags)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	/*
	 * If the RW or ROLLBACK banks are protected but the RO banks aren't,
	 * that's inconsistent.
	 *
	 * Note that we check this before adding in the physical flags below,
	 * since some chips can also protect ALL_NOW for the current boot by
	 * locking up the flash program-erase registers.
	 */
	if ((flags & all_flags) && !(flags & EC_FLASH_PROTECT_RO_NOW))
		flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;

#ifndef CONFIG_FLASH_PROTECT_RW
	/* RW flag was used for intermediate computations, clear it now. */
	flags &= ~EC_FLASH_PROTECT_RW_NOW;
#endif

	/* Add in flags from physical layer */
	return flags | flash_physical_get_protect_flags();
}

/*
 * Request a flash protection flags change for |mask| flash protect flags
 * to |flags| state.
 *
 * Order of flag processing:
 * 1. Clear/Set RO_AT_BOOT + Clear *_AT_BOOT flags + Commit *_AT_BOOT flags.
 * 2. Return if RO_AT_BOOT and HW-WP are not asserted.
 * 3. Set remaining *_AT_BOOT flags + Commit *_AT_BOOT flags.
 * 4. Commit RO_NOW.
 * 5. Commit ALL_NOW.
 */
int flash_set_protect(uint32_t mask, uint32_t flags)
{
	int retval = EC_SUCCESS;
	int rv;
	int old_flags_at_boot = flash_get_protect() &
		(EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RW_AT_BOOT |
			EC_FLASH_PROTECT_ROLLBACK_AT_BOOT |
			EC_FLASH_PROTECT_ALL_AT_BOOT);
	int new_flags_at_boot = old_flags_at_boot;

	/* Sanitize input flags */
	flags = flags & mask;

	/*
	 * Process flags we can set.  Track the most recent error, but process
	 * all flags before returning.
	 */

	/*
	 * AT_BOOT flags are trickier than NOW flags, as they can be set
	 * when HW write protection is disabled and can be unset without
	 * a reboot.
	 *
	 * If we are only setting/clearing RO_AT_BOOT, things are simple.
	 * Setting ALL_AT_BOOT is processed only if HW write protection is
	 * enabled and RO_AT_BOOT is set, so it's also simple.
	 *
	 * The most tricky one is when we want to clear ALL_AT_BOOT. We need
	 * to determine whether to clear protection for the entire flash or
	 * leave RO protected. There are two cases that we want to keep RO
	 * protected:
	 *   A. RO_AT_BOOT was already set before flash_set_protect() is
	 *      called.
	 *   B. RO_AT_BOOT was not set, but it's requested to be set by
	 *      the caller of flash_set_protect().
	 */

	/* 1.a - Clear RO_AT_BOOT. */
	new_flags_at_boot &= ~(mask & EC_FLASH_PROTECT_RO_AT_BOOT);
	/* 1.b - Set RO_AT_BOOT. */
	new_flags_at_boot |= flags & EC_FLASH_PROTECT_RO_AT_BOOT;

	/* 1.c - Clear ALL_AT_BOOT. */
	if ((mask & EC_FLASH_PROTECT_ALL_AT_BOOT) &&
	    !(flags & EC_FLASH_PROTECT_ALL_AT_BOOT)) {
		new_flags_at_boot &= ~EC_FLASH_PROTECT_ALL_AT_BOOT;
		/* Must also clear RW/ROLLBACK. */
#ifdef CONFIG_FLASH_PROTECT_RW
		new_flags_at_boot &= ~EC_FLASH_PROTECT_RW_AT_BOOT;
#endif
#ifdef CONFIG_ROLLBACK
		new_flags_at_boot &= ~EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif
	}

	/* 1.d - Clear RW_AT_BOOT. */
#ifdef CONFIG_FLASH_PROTECT_RW
	if ((mask & EC_FLASH_PROTECT_RW_AT_BOOT) &&
	    !(flags & EC_FLASH_PROTECT_RW_AT_BOOT)) {
		new_flags_at_boot &= ~EC_FLASH_PROTECT_RW_AT_BOOT;
		/* Must also clear ALL (otherwise nothing will happen). */
		new_flags_at_boot &= ~EC_FLASH_PROTECT_ALL_AT_BOOT;
	}
#endif

	/* 1.e - Clear ROLLBACK_AT_BOOT. */
#ifdef CONFIG_ROLLBACK
	if ((mask & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) &&
	    !(flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT)) {
		new_flags_at_boot &= ~EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
		/* Must also remove ALL (otherwise nothing will happen). */
		new_flags_at_boot &= ~EC_FLASH_PROTECT_ALL_AT_BOOT;
	}
#endif

	/* 1.f - Commit *_AT_BOOT "clears" (and RO "set" 1.b). */
	if (new_flags_at_boot != old_flags_at_boot) {
		rv = flash_protect_at_boot(new_flags_at_boot);
		if (rv)
			retval = rv;
		old_flags_at_boot = new_flags_at_boot;
	}

	/* 2 - Return if RO_AT_BOOT and HW-WP are not asserted.
	 *
	 * All subsequent flags only work if write protect is enabled (that is,
	 * hardware WP flag) *and* RO is protected at boot (software WP flag).
	 */
	if ((~flash_get_protect()) & (EC_FLASH_PROTECT_GPIO_ASSERTED |
				      EC_FLASH_PROTECT_RO_AT_BOOT))
		return retval;

	/*
	 * 3.a - Set ALL_AT_BOOT.
	 *
	 * The case where ALL/RW/ROLLBACK_AT_BOOT is cleared is already covered
	 * above, so we do not need to mask it out.
	 */
	new_flags_at_boot |= flags & EC_FLASH_PROTECT_ALL_AT_BOOT;

	/* 3.b - Set RW_AT_BOOT. */
#ifdef CONFIG_FLASH_PROTECT_RW
	new_flags_at_boot |= flags & EC_FLASH_PROTECT_RW_AT_BOOT;
#endif

	/* 3.c - Set ROLLBACK_AT_BOOT. */
#ifdef CONFIG_ROLLBACK
	new_flags_at_boot |= flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif

	/* 3.d - Commit *_AT_BOOT "sets". */
	if (new_flags_at_boot != old_flags_at_boot) {
		rv = flash_protect_at_boot(new_flags_at_boot);
		if (rv)
			retval = rv;
	}

	/* 4 - Commit RO_NOW. */
	if (flags & EC_FLASH_PROTECT_RO_NOW) {
		rv = flash_physical_protect_now(0);
		if (rv)
			retval = rv;
	}

	/* 5 - Commit ALL_NOW. */
	if (flags & EC_FLASH_PROTECT_ALL_NOW) {
		rv = flash_physical_protect_now(1);
		if (rv)
			retval = rv;
	}

	return retval;
}

#ifdef CONFIG_FLASH_DEFERRED_ERASE
static volatile enum ec_status erase_rc = EC_RES_SUCCESS;
static struct ec_params_flash_erase_v1 erase_info;

static void flash_erase_deferred(void)
{
	erase_rc = EC_RES_BUSY;
	if (flash_erase(erase_info.params.offset, erase_info.params.size))
		erase_rc = EC_RES_ERROR;
	else
		erase_rc = EC_RES_SUCCESS;
}
DECLARE_DEFERRED(flash_erase_deferred);
#endif

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_FLASHINFO
static int command_flash_info(int argc, char **argv)
{
	int i, flags;

	ccprintf("Usable:  %4d KB\n", CONFIG_FLASH_SIZE / 1024);
	ccprintf("Write:   %4d B (ideal %d B)\n", CONFIG_FLASH_WRITE_SIZE,
		 CONFIG_FLASH_WRITE_IDEAL_SIZE);
#ifdef CONFIG_FLASH_MULTIPLE_REGION
	ccprintf("Regions:\n");
	for (i = 0; i < ARRAY_SIZE(flash_bank_array); i++) {
		ccprintf(" %d region%s:\n",
			 flash_bank_array[i].count,
			 (flash_bank_array[i].count == 1 ? "" : "s"));
		ccprintf("  Erase:   %4d B (to %d-bits)\n",
			 1 << flash_bank_array[i].erase_size_exp,
			 CONFIG_FLASH_ERASED_VALUE32 ? 1 : 0);
		ccprintf("  Size/Protect: %4d B\n",
			 1 << flash_bank_array[i].size_exp);
	}
#else
	ccprintf("Erase:   %4d B (to %d-bits)\n", CONFIG_FLASH_ERASE_SIZE,
		 CONFIG_FLASH_ERASED_VALUE32 ? 1 : 0);
	ccprintf("Protect: %4d B\n", CONFIG_FLASH_BANK_SIZE);
#endif
	flags = flash_get_protect();
	ccprintf("Flags:  ");
	if (flags & EC_FLASH_PROTECT_GPIO_ASSERTED)
		ccputs(" wp_gpio_asserted");
	if (flags & EC_FLASH_PROTECT_RO_AT_BOOT)
		ccputs(" ro_at_boot");
	if (flags & EC_FLASH_PROTECT_ALL_AT_BOOT)
		ccputs(" all_at_boot");
	if (flags & EC_FLASH_PROTECT_RO_NOW)
		ccputs(" ro_now");
	if (flags & EC_FLASH_PROTECT_ALL_NOW)
		ccputs(" all_now");
#ifdef CONFIG_FLASH_PROTECT_RW
	if (flags & EC_FLASH_PROTECT_RW_AT_BOOT)
		ccputs(" rw_at_boot");
	if (flags & EC_FLASH_PROTECT_RW_NOW)
		ccputs(" rw_now");
#endif
	if (flags & EC_FLASH_PROTECT_ERROR_STUCK)
		ccputs(" STUCK");
	if (flags & EC_FLASH_PROTECT_ERROR_INCONSISTENT)
		ccputs(" INCONSISTENT");
#ifdef CONFIG_ROLLBACK
	if (flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT)
		ccputs(" rollback_at_boot");
	if (flags & EC_FLASH_PROTECT_ROLLBACK_NOW)
		ccputs(" rollback_now");
#endif
	ccputs("\n");

	ccputs("Protected now:");
	for (i = 0; i < PHYSICAL_BANKS; i++) {
		if (!(i & 31))
			ccputs("\n    ");
		else if (!(i & 7))
			ccputs(" ");
		ccputs(flash_physical_get_protect(i) ? "Y" : ".");
	}
	ccputs("\n");
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(flashinfo, command_flash_info,
			     NULL,
			     "Print flash info");
#endif /* CONFIG_CMD_FLASHINFO */

#ifdef CONFIG_CMD_FLASH
static int command_flash_erase(int argc, char **argv)
{
	int offset = -1;
	int size = -1;
	int rv;

	if (flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		return EC_ERROR_ACCESS_DENIED;

	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;

	ccprintf("Erasing %d bytes at 0x%x...\n", size, offset);
	return eflash_debug_physical_erase(offset, size);
}
DECLARE_CONSOLE_COMMAND(flasherase, command_flash_erase,
			"offset size",
			"Erase flash");

static int command_flash_write(int argc, char **argv)
{
	int offset = -1;
	int size = -1;
	int rv;
	char *data;
	int i;
	char dst_var;

	if (flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		return EC_ERROR_ACCESS_DENIED;

	rv = parse_offset_size_value(argc, argv, 1, &offset, &size, &dst_var);
	if (rv)
		return rv;

	if (size > shared_mem_size())
		size = shared_mem_size();

	/* Acquire the shared memory buffer */
	rv = shared_mem_acquire(size, &data);
	if (rv) {
		ccputs("Can't get shared mem\n");
		return rv;
	}

	/* Fill the data buffer with a pattern */
	for (i = 0; i < size; i++)
		data[i] = dst_var;

	ccprintf("Writing %d bytes to 0x%x...:%02x, from:%x\n", size, offset, 
		(unsigned char)dst_var, (unsigned int)data);
	rv = flash_write(offset, size, data);
	if (rv) {
		ccprintf("flashwrite error:%d\n", rv);
		return rv;
	}

	/* Free the buffer */
	shared_mem_release(data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(flashwrite, command_flash_write,
			"offset size",
			"Write pattern to flash");

static int command_flash_read(int argc, char **argv)
{
	int offset = -1;
	int size = 256;
	int rv;
	char *data;
	int i;

	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;

	if (size > shared_mem_size())
		size = shared_mem_size();

	/* Acquire the shared memory buffer */
	rv = shared_mem_acquire(size, &data);
	if (rv) {
		ccputs("Can't get shared mem\n");
		return rv;
	}

	/* Read the data */
	if (flash_read(offset, size, data)) {
		shared_mem_release(data);
		return EC_ERROR_INVAL;
	}

	ccprintf("%08x: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f", offset);

	/* Dump it */
	for (i = 0; i < size; i++) {
		if ((offset + i) % 16) {
			ccprintf(" %02x", data[i]);
		} else {
			ccprintf("\n%08x: %02x", offset + i, data[i]);
			cflush();
		}
	}
	ccprintf("\n");

	/* Free the buffer */
	shared_mem_release(data);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flashread, command_flash_read,
			"offset [size]",
			"Read flash");
#endif

static int command_flash_wp(int argc, char **argv)
{
	int val;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "now"))
		return flash_set_protect(EC_FLASH_PROTECT_ALL_NOW, -1);

	if (!strcasecmp(argv[1], "all"))
		return flash_set_protect(EC_FLASH_PROTECT_ALL_AT_BOOT, -1);

	if (!strcasecmp(argv[1], "noall"))
		return flash_set_protect(EC_FLASH_PROTECT_ALL_AT_BOOT, 0);

#ifdef CONFIG_FLASH_PROTECT_RW
	if (!strcasecmp(argv[1], "rw"))
		return flash_set_protect(EC_FLASH_PROTECT_RW_AT_BOOT, -1);

	if (!strcasecmp(argv[1], "norw"))
		return flash_set_protect(EC_FLASH_PROTECT_RW_AT_BOOT, 0);
#endif

#ifdef CONFIG_ROLLBACK
	if (!strcasecmp(argv[1], "rb"))
		return flash_set_protect(EC_FLASH_PROTECT_ROLLBACK_AT_BOOT, -1);

	if (!strcasecmp(argv[1], "norb"))
		return flash_set_protect(EC_FLASH_PROTECT_ROLLBACK_AT_BOOT, 0);
#endif

	/* Do this last, since anything starting with 'n' means "no" */
	if (parse_bool(argv[1], &val))
		return flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
					 val ? -1 : 0);

	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND(flashwp, command_flash_wp,
			"<BOOLEAN> | now | all | noall"
#ifdef CONFIG_FLASH_PROTECT_RW
			" | rw | norw"
#endif
#ifdef CONFIG_ROLLBACK
			" | rb | norb"
#endif
			, "Modify flash write protect");

/*****************************************************************************/
/* Host commands */

/*
 * All internal EC code assumes that offsets are provided relative to
 * physical address zero of storage. In some cases, the region of storage
 * belonging to the EC is not physical address zero - a non-zero fmap_base
 * indicates so. Since fmap_base is not yet handled correctly by external
 * code, we must perform the adjustment in our host command handlers -
 * adjust all offsets so they are relative to the beginning of the storage
 * region belonging to the EC. TODO(crbug.com/529365): Handle fmap_base
 * correctly in flashrom, dump_fmap, etc. and remove EC_FLASH_REGION_START.
 */
#define EC_FLASH_REGION_START MIN(CONFIG_EC_PROTECTED_STORAGE_OFF, \
				  CONFIG_EC_WRITABLE_STORAGE_OFF)

static enum ec_status flash_command_get_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_info_2 *p_2 = args->params;
	struct ec_response_flash_info_2 *r_2 = args->response;
#ifdef CONFIG_FLASH_MULTIPLE_REGION
	int banks_size = ARRAY_SIZE(flash_bank_array);
	const struct ec_flash_bank *banks = flash_bank_array;
#else
	struct ec_response_flash_info_1 *r_1 = args->response;
#if CONFIG_FLASH_BANK_SIZE < CONFIG_FLASH_ERASE_SIZE
#error "Flash: Bank size expected bigger or equal to erase size."
#endif
	struct ec_flash_bank single_bank = {
		.count = CONFIG_FLASH_SIZE / CONFIG_FLASH_BANK_SIZE,
		.size_exp = __fls(CONFIG_FLASH_BANK_SIZE),
		.write_size_exp = __fls(CONFIG_FLASH_WRITE_SIZE),
		.erase_size_exp = __fls(CONFIG_FLASH_ERASE_SIZE),
		.protect_size_exp = __fls(CONFIG_FLASH_BANK_SIZE),
	};
	int banks_size = 1;
	const struct ec_flash_bank *banks = &single_bank;
#endif
	int banks_len;
	int ideal_size;

	/*
	 * Compute the ideal amount of data for the host to send us,
	 * based on the maximum response size and the ideal write size.
	 */
	ideal_size = (args->response_max -
		 sizeof(struct ec_params_flash_write)) &
		~(CONFIG_FLASH_WRITE_IDEAL_SIZE - 1);
	/*
	 * If we can't get at least one ideal block, then just want
	 * as high a multiple of the minimum write size as possible.
	 */
	if (!ideal_size)
		ideal_size = (args->response_max -
				sizeof(struct ec_params_flash_write)) &
				~(CONFIG_FLASH_WRITE_SIZE - 1);


	if (args->version >= 2) {
		args->response_size = sizeof(struct ec_response_flash_info_2);
		r_2->flash_size = CONFIG_FLASH_SIZE - EC_FLASH_REGION_START;
#if (CONFIG_FLASH_ERASED_VALUE32 == 0)
		r_2->flags = EC_FLASH_INFO_ERASE_TO_0;
#else
		r_2->flags = 0;
#endif
#ifdef CONFIG_FLASH_SELECT_REQUIRED
		r_2->flags |= EC_FLASH_INFO_SELECT_REQUIRED;
#endif
		r_2->write_ideal_size = ideal_size;
		r_2->num_banks_total = banks_size;
		r_2->num_banks_desc = MIN(banks_size, p_2->num_banks_desc);
		banks_len = r_2->num_banks_desc * sizeof(struct ec_flash_bank);
		memcpy(r_2->banks, banks, banks_len);
		args->response_size += banks_len;
		return EC_RES_SUCCESS;
	}
#ifdef CONFIG_FLASH_MULTIPLE_REGION
	return EC_RES_INVALID_PARAM;
#else
	r_1->flash_size = CONFIG_FLASH_SIZE - EC_FLASH_REGION_START;
	r_1->flags = 0;
	r_1->write_block_size = CONFIG_FLASH_WRITE_SIZE;
	r_1->erase_block_size = CONFIG_FLASH_ERASE_SIZE;
	r_1->protect_block_size = CONFIG_FLASH_BANK_SIZE;
	if (args->version == 0) {
		/* Only version 0 fields returned */
		args->response_size = sizeof(struct ec_response_flash_info);
	} else {
		args->response_size = sizeof(struct ec_response_flash_info_1);
		/* Fill in full version 1 struct */
		r_1->write_ideal_size = ideal_size;
#if (CONFIG_FLASH_ERASED_VALUE32 == 0)
		r_1->flags |= EC_FLASH_INFO_ERASE_TO_0;
#endif
#ifdef CONFIG_FLASH_SELECT_REQUIRED
		r_1->flags |= EC_FLASH_INFO_SELECT_REQUIRED;
#endif
	}
	return EC_RES_SUCCESS;
#endif  /* CONFIG_FLASH_MULTIPLE_REGION */
}
#ifdef CONFIG_FLASH_MULTIPLE_REGION
#define FLASH_INFO_VER EC_VER_MASK(2)
#else
#define FLASH_INFO_VER (EC_VER_MASK(0) | EC_VER_MASK(1) | EC_VER_MASK(2))
#endif
DECLARE_HOST_COMMAND(EC_CMD_FLASH_INFO,
		     flash_command_get_info, FLASH_INFO_VER);


static enum ec_status flash_command_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_read *p = args->params;
	uint32_t offset = p->offset + EC_FLASH_REGION_START;

	if (p->size > args->response_max)
		return EC_RES_OVERFLOW;

	if (flash_read(offset, p->size, args->response))
		return EC_RES_ERROR;

	args->response_size = p->size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_READ,
		     flash_command_read,
		     EC_VER_MASK(0));

/**
 * Flash write command
 *
 * Version 0 and 1 are equivalent from the EC-side; the only difference is
 * that the host can only send 64 bytes of data at a time in version 0.
 */
static enum ec_status flash_command_write(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_write *p = args->params;
	uint32_t offset = p->offset + EC_FLASH_REGION_START;

	if (flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		return EC_RES_ACCESS_DENIED;

	if (p->size + sizeof(*p) > args->params_size)
		return EC_RES_INVALID_PARAM;

#ifdef CONFIG_INTERNAL_STORAGE
	if (system_unsafe_to_overwrite(offset, p->size))
		return EC_RES_ACCESS_DENIED;
#endif

	if (flash_write(offset, p->size, (const uint8_t *)(p + 1)))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_WRITE,
		     flash_command_write,
		     EC_VER_MASK(0) | EC_VER_MASK(EC_VER_FLASH_WRITE));

#ifndef CONFIG_FLASH_MULTIPLE_REGION
/*
 * Make sure our image sizes are a multiple of flash block erase size so that
 * the host can erase the entire image.
 */
BUILD_ASSERT(CONFIG_RO_SIZE % CONFIG_FLASH_ERASE_SIZE == 0);
BUILD_ASSERT(CONFIG_RW_SIZE % CONFIG_FLASH_ERASE_SIZE == 0);
BUILD_ASSERT(EC_FLASH_REGION_RO_SIZE % CONFIG_FLASH_ERASE_SIZE == 0);
BUILD_ASSERT(CONFIG_EC_WRITABLE_STORAGE_SIZE % CONFIG_FLASH_ERASE_SIZE == 0);

#endif

static enum ec_status flash_command_erase(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_erase *p = args->params;
	int rc = EC_RES_SUCCESS, cmd = FLASH_ERASE_SECTOR;
	uint32_t offset;
#ifdef CONFIG_FLASH_DEFERRED_ERASE
	const struct ec_params_flash_erase_v1 *p_1 = args->params;

	if (args->version > 0) {
		cmd = p_1->cmd;
		p = &p_1->params;
	}
#endif
	offset = p->offset + EC_FLASH_REGION_START;

	if (flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		return EC_RES_ACCESS_DENIED;

#ifdef CONFIG_INTERNAL_STORAGE
	if (system_unsafe_to_overwrite(offset, p->size))
		return EC_RES_ACCESS_DENIED;
#endif

	switch (cmd) {
	case FLASH_ERASE_SECTOR:
#if defined(HAS_TASK_HOSTCMD) && defined(CONFIG_HOST_COMMAND_STATUS)
		args->result = EC_RES_IN_PROGRESS;
		host_send_response(args);
#endif
		if (flash_erase(offset, p->size))
			return EC_RES_ERROR;

		break;
#ifdef CONFIG_FLASH_DEFERRED_ERASE
	case FLASH_ERASE_SECTOR_ASYNC:
		rc = erase_rc;
		if (rc == EC_RES_SUCCESS) {
			memcpy(&erase_info, p_1, sizeof(*p_1));
			hook_call_deferred(&flash_erase_deferred_data,
					   100 * MSEC);
		} else {
			/*
			 * Not our job to return the result of
			 * the previous command.
			 */
			rc = EC_RES_BUSY;
		}
		break;
	case FLASH_ERASE_GET_RESULT:
		rc = erase_rc;
		if (rc != EC_RES_BUSY)
			/* Ready for another command */
			erase_rc = EC_RES_SUCCESS;
		break;
#endif
	default:
		rc = EC_RES_INVALID_PARAM;
	}
	return rc;
}


DECLARE_HOST_COMMAND(EC_CMD_FLASH_ERASE, flash_command_erase,
		EC_VER_MASK(0)
#ifdef CONFIG_FLASH_DEFERRED_ERASE
		| EC_VER_MASK(1)
#endif
		);

static enum ec_status flash_command_protect(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_protect *p = args->params;
	struct ec_response_flash_protect *r = args->response;

	/*
	 * Handle requesting new flags.  Note that we ignore the return code
	 * from flash_set_protect(), since errors will be visible to the caller
	 * via the flags in the response.  (If we returned error, the caller
	 * wouldn't get the response.)
	 */
	if (p->mask)
		flash_set_protect(p->mask, p->flags);

	/*
	 * Retrieve the current flags.  The caller can use this to determine
	 * which of the requested flags could be set.  This is cleaner than
	 * simply returning error, because it provides information to the
	 * caller about the actual result.
	 */
	r->flags = flash_get_protect();

	/* Indicate which flags are valid on this platform */
	r->valid_flags =
		EC_FLASH_PROTECT_GPIO_ASSERTED |
		EC_FLASH_PROTECT_ERROR_STUCK |
		EC_FLASH_PROTECT_ERROR_INCONSISTENT |
		flash_physical_get_valid_flags();
	r->writable_flags = flash_physical_get_writable_flags(r->flags);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}

/*
 * TODO(crbug.com/239197) : Adding both versions to the version mask is a
 * temporary workaround for a problem in the cros_ec driver. Drop
 * EC_VER_MASK(0) once cros_ec driver can send the correct version.
 */
DECLARE_HOST_COMMAND(EC_CMD_FLASH_PROTECT,
		     flash_command_protect,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static enum ec_status
flash_command_region_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_region_info *p = args->params;
	struct ec_response_flash_region_info *r = args->response;

	switch (p->region) {
	case EC_FLASH_REGION_RO:
		r->offset = CONFIG_EC_PROTECTED_STORAGE_OFF +
			    CONFIG_RO_STORAGE_OFF -
			    EC_FLASH_REGION_START;
		r->size = EC_FLASH_REGION_RO_SIZE;
		break;
	case EC_FLASH_REGION_ACTIVE:
		r->offset = flash_get_rw_offset(system_get_active_copy()) -
				EC_FLASH_REGION_START;
		r->size = CONFIG_EC_WRITABLE_STORAGE_SIZE;
		break;
	case EC_FLASH_REGION_WP_RO:
		r->offset = CONFIG_WP_STORAGE_OFF -
			    EC_FLASH_REGION_START;
		r->size = CONFIG_WP_STORAGE_SIZE;
		break;
	case EC_FLASH_REGION_UPDATE:
		r->offset = flash_get_rw_offset(system_get_update_copy()) -
				EC_FLASH_REGION_START;
		r->size = CONFIG_EC_WRITABLE_STORAGE_SIZE;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_REGION_INFO,
		     flash_command_region_info,
		     EC_VER_MASK(EC_VER_FLASH_REGION_INFO));


#ifdef CONFIG_FLASH_SELECT_REQUIRED

static enum ec_status flash_command_select(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_select *p = args->params;

	return board_flash_select(p->select);
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_SELECT,
		     flash_command_select,
		     EC_VER_MASK(0));

#endif /* CONFIG_FLASH_SELECT_REQUIRED */

#if defined(CONFIG_FLASH_LOG_OEM)
/*******************************************************************************
*       NPCX796-512K eFlash layout    |
*                                     | NPCX796FC, it has 512K of flash space 
*      |--------| 0x8000              | as the code flash area. It's split
*      |  64K   |                     | in two, one as the RO region and one as 
*      |        |                     | the RW region. But the size of RO/RW
*      |--------| 0x7000(RW=192K)     | regions is 192K, there are two 64K 
*      |        |                     | reserved areas. 
*      |        |                     |
*      |        |                     | We use the 8K storage area to hold some 
*      |        | RW                  | power on/off events, even if the power
*      |        |                     | is lost, the data is not lost.
*      |--------| 0x4000              |
*      |  64K   |                     | Shutdown events are stored at 0x3C000
*      |        |                     | in 4K storage area.
*      |--------| 0x3000(RO=192K)     | PowerOn events are stored at 0x3D000
*      |        |                     | in 4K storage area.
*      |        |                     |
*      |        |                     |
*      |        | RO                  |
*      |        |                     |
*      |--------| 0x0000              |
*
*   shutdown cause :
*       offset : 0x3C000    size : 1000(4K)
*       The 128 bytes are divided into a page, there are 32 PAGES in 4K.
*       page-0 for data header, page-1 ~ page31 for shutdown data
*
*   wakeup cause :
*       offset : 0x3D000    size : 1000(4K)
*       The 128 bytes are divided into a page, there are 32 PAGES in 4K.
*       page-0 for data header, page-1 ~ page31 for shutdown data
*
*******************************************************************************/
#define SHUTDOWN_RANGE_START    0x3C000 // shutdown cause start address
#define SHUTDOWN_RANGE_SIZE     0x1000  // 4K
#define SHUTDOWN_HEADER_OFFSET  SHUTDOWN_RANGE_START
#define SHUTDOWN_HEADER_SIZE    0x80    // Don't modify
#define SHUTDOWN_DATA_OFFSET    (SHUTDOWN_HEADER_OFFSET + SHUTDOWN_HEADER_SIZE)
#define SHUTDOWN_DATA_SIZE      (SHUTDOWN_RANGE_SIZE - SHUTDOWN_HEADER_SIZE)
#define SHUTDOWN_RANGE_END      (SHUTDOWN_RANGE_START + SHUTDOWN_RANGE_SIZE)

#define WAKEUP_RANGE_START      SHUTDOWN_RANGE_END
#define WAKEUP_RANGE_SIZE       SHUTDOWN_RANGE_SIZE
#define WAKEUP_HEADER_OFFSET    WAKEUP_RANGE_START
#define WAKEUP_HEADER_SIZE      SHUTDOWN_HEADER_SIZE
#define WAKEUP_DATA_OFFSET      (WAKEUP_HEADER_OFFSET+WAKEUP_HEADER_SIZE)
#define WAKEUP_DATA_SIZE        (WAKEUP_RANGE_SIZE - WAKEUP_HEADER_SIZE)
#define WAKEUP_RANGE_END        (WAKEUP_RANGE_START + WAKEUP_RANGE_SIZE)

#define DATA_PAGE_SIZE          SHUTDOWN_HEADER_SIZE
#define DATA_PAGE_NUM           0x20
#define LOG_SIZE                0x08    // Must be 8-byte/16-byte/32-byte aligned

#define CAUSE_LOG_CELL_SIZE     4           // cause id and timestamp, every one should be 4 bytes
#define CAUSE_LOG_INVALID       0xffffffff  // flash after erase, initial value should be empty

uint32_t shutdown_write_index;
uint32_t wakeup_write_index;
uint8_t g_abnormalPowerDownTimes;

/**
 * shutdown cause eFlash debug init
 *
 * Read header data makes it easy to find the next write location.
 *
 */
static int shutdown_eflash_debug_init(void)
{
    uint32_t data_index;
    uint32_t page_index;
    __aligned(CAUSE_LOG_CELL_SIZE) uint8_t eflash_data_header[256];
    uint32_t *shutdown_cause_p = (uint32_t *)eflash_data_header;
    uint32_t shutdown_cause_data;
    int status = EC_SUCCESS;

    //------------------------------------------------------------------
    // read shutdown data header, Look for pages that aren't full
    status = flash_read(SHUTDOWN_HEADER_OFFSET,
                        SHUTDOWN_HEADER_SIZE, eflash_data_header);
    if(EC_ERROR(status)) {
        return status;
    }

    for(page_index=1; page_index<DATA_PAGE_NUM; page_index++) {
        if(0xFF == eflash_data_header[page_index]) {
            break;
        }
    }

    // Reads pages that are not full, Find the location that was not written
    status = flash_read(SHUTDOWN_HEADER_OFFSET+(page_index*DATA_PAGE_SIZE),
                        DATA_PAGE_SIZE, eflash_data_header);
    if(EC_ERROR(status)) {
        return status;
    }

    for(data_index=0; data_index<(DATA_PAGE_SIZE/CAUSE_LOG_CELL_SIZE); data_index++) {
        shutdown_cause_data = shutdown_cause_p[data_index];
        if(CAUSE_LOG_INVALID == shutdown_cause_data) {
            shutdown_write_index = (uint32_t)(SHUTDOWN_HEADER_OFFSET +
                                   (page_index*DATA_PAGE_SIZE) +
                                   (data_index*CAUSE_LOG_CELL_SIZE));
            
            ccprintf("====== page_index = [%x], shutdown_write_index = [%x]\n",
                        page_index, shutdown_write_index);
            break;
        }
    }
    return status;
}

/**
 * wakeup cause eFlash debug init
 *
 * Read header data makes it easy to find the next write location.
 *
 */
static int wakeup_eflash_debug_init(void)
{
    uint32_t data_index;
    uint32_t page_index;
    __aligned(CAUSE_LOG_CELL_SIZE) uint8_t eflash_data_header[256];
    uint32_t *wakeup_cause_p = (uint32_t *)eflash_data_header;
    uint32_t wakeup_cause_data;
    int status = EC_SUCCESS;

    //------------------------------------------------------------------
    // read wakeup data header, Look for pages that aren't full
    status = flash_read(WAKEUP_HEADER_OFFSET, WAKEUP_HEADER_SIZE, eflash_data_header);
    if(EC_ERROR(status)) {
        return status;
    }
    
    for(page_index=1; page_index<DATA_PAGE_NUM; page_index++) {
        if(0xFF == eflash_data_header[page_index]) {
            break;
        }
    }

    // Reads pages that are not full, Find the location that was not written
    status = flash_read(WAKEUP_HEADER_OFFSET+(page_index*DATA_PAGE_SIZE),
        DATA_PAGE_SIZE, eflash_data_header);
    if(EC_ERROR(status)) {
        return status;
    }

    for(data_index=0; data_index < (DATA_PAGE_SIZE / CAUSE_LOG_CELL_SIZE); data_index++) {
        wakeup_cause_data = wakeup_cause_p[data_index];
        if(CAUSE_LOG_INVALID == wakeup_cause_data) {
            wakeup_write_index = (uint32_t)(WAKEUP_HEADER_OFFSET +
                                   (page_index * DATA_PAGE_SIZE) +
                                   data_index * CAUSE_LOG_CELL_SIZE);
            ccprintf("====== page_index = [%x], wakeup_write_index = [%x]\n",
                        page_index, wakeup_write_index);
            break;
        }
    }
    return status;
}

/**
 * eFlash debug init
 *
 * Read header data makes it easy to find the next write location.
 *
 */
static void eflash_debug_init(void)
{
    /* shutdown cause eFlash debug init */
    if (EC_ERROR(shutdown_eflash_debug_init())) {
        ccprintf("====== ERROR: shutdown cause eFlash debug init");
    }

    /* wakeup cause eFlash debug init */
    if (EC_ERROR(wakeup_eflash_debug_init())) {
        ccprintf("====== ERROR: wakeup cause eFlash debug init");
    }
}
DECLARE_HOOK(HOOK_INIT, eflash_debug_init, HOOK_PRIO_DEFAULT);

/**
 * shutdown cause record
 *
 * write shutdown cause 16-bytes to eFlash, and look for the location of 
 * the next write.
 * Write header to 0xAA after a page(128-bytes) is full.
 * Erase shutdown cause data when all the pages(32-pages) are full.
 */
void shutdown_cause_record(uint32_t data)
{
    uint8_t  full_flag[8] = {0};
    uint32_t eFlash_Data[8]={0};
    uint32_t page_index;
    uint32_t base_address;
    uint32_t end_address;
    uint32_t write_index;
    struct ec_params_flash_log log_Data;

    if ((data >> 16) & 0x01) {
        set_abnormal_shutdown(0x01);
    }

    // check shutdown cause write index
    base_address = (uint32_t)SHUTDOWN_DATA_OFFSET;
    end_address = (uint32_t)(SHUTDOWN_DATA_OFFSET+SHUTDOWN_DATA_SIZE);
    if((shutdown_write_index<base_address) ||
       (shutdown_write_index>=end_address))
    {
        eflash_debug_init();
    }

    if((shutdown_write_index<base_address) ||
       (shutdown_write_index>=end_address))
    {
        ccprintf("====== shutdown_write_index[%x] out of range !!!\n",
                shutdown_write_index);
        return;
    }

    // 8-byte alignment
    if(shutdown_write_index & (LOG_SIZE-1))
    {
		ccprintf("====== shutdown index(%08x) not aligned cause, adjust\n", shutdown_write_index);
		write_index = shutdown_write_index - (shutdown_write_index&(LOG_SIZE-1));
		log_Data.log_timestamp = 01;
		log_Data.log_id = LOG_ID_SHUTDOWN_0x08;
		/* adjust not aligned cause, write */
		if(flash_write(write_index, LOG_SIZE, (const char *)(&log_Data))) {
			ccprintf("====== shutdown index not aligned cause, write fail\n");
		}

		write_index = LOG_SIZE - (shutdown_write_index&(LOG_SIZE-1));
		shutdown_write_index += write_index;
    }

    ccprintf("====== shutdown log [%02x] -> [%x]\n", (uint16_t)data, shutdown_write_index);

    // add timestamp
    log_Data.log_timestamp = NPCX_TTC;
    log_Data.log_id = data;
    
    // write shutdown cause
    if(flash_write(shutdown_write_index, LOG_SIZE, (const char *)(&log_Data)))
    {
        return;
    }

    // write page full flag
    shutdown_write_index += LOG_SIZE;
    if(!(shutdown_write_index&(DATA_PAGE_SIZE-1))) // 128-byte page full
    {
        full_flag[0] = 0xAA;
        page_index = ((shutdown_write_index - base_address)/DATA_PAGE_SIZE);
        
        ccprintf("====== shutdown page full, page_index = [%x]\n", page_index);

        flash_write(SHUTDOWN_HEADER_OFFSET+page_index, 1, full_flag);
    }

    // All the pages are full
    if(SHUTDOWN_RANGE_END == shutdown_write_index)
    {
        ccprintf("====== shutdown range full, shutdown_write_index=[%x] erease start[%x] size[%x]\n",
                    shutdown_write_index, SHUTDOWN_RANGE_START, SHUTDOWN_RANGE_SIZE);

        // read last 4 log
        flash_read((SHUTDOWN_RANGE_END-(4*LOG_SIZE)), (4*LOG_SIZE), (char *)eFlash_Data);

        // erase 4K
        if(eflash_debug_physical_erase(SHUTDOWN_RANGE_START, SHUTDOWN_RANGE_SIZE))
        {
            shutdown_write_index = 0;
        }
        else
        {
            shutdown_write_index = SHUTDOWN_DATA_OFFSET;
            // write last 4
            flash_write(shutdown_write_index, (4*LOG_SIZE), (char *)eFlash_Data);
            shutdown_write_index = SHUTDOWN_DATA_OFFSET+(4*LOG_SIZE);
        }
    }
}

/**
 * wakeup cause record
 *
 * write wakeup cause 16-bytes to eFlash, and look for the location of 
 * the next write.
 * Write header to 0xAA after a page(128-bytes) is full.
 * Erase wakeup cause data when all the pages(32-pages) are full.
 */
void wakeup_cause_record(uint32_t data)
{
    uint8_t  full_flag[8] = {0};
    uint32_t eFlash_Data[8]={0};
    uint32_t page_index;
    uint32_t base_address;
    uint32_t end_address;
    uint32_t write_index;
    struct ec_params_flash_log log_Data;

    // check wakeup cause write index
    base_address = (uint32_t)WAKEUP_DATA_OFFSET;
    end_address = (uint32_t)(WAKEUP_DATA_OFFSET+WAKEUP_DATA_SIZE);
    if((wakeup_write_index<base_address) ||
       (wakeup_write_index>=end_address))
    {
        eflash_debug_init();
    }

    if((wakeup_write_index<base_address) ||
       (wakeup_write_index>=end_address))
    {
        ccprintf("====== wakeup_write_index out of range [%x]\n",
                wakeup_write_index);
        return;
    }

    // 8-byte alignment
    if(wakeup_write_index & (LOG_SIZE-1))
    {
        write_index = LOG_SIZE - (wakeup_write_index&(LOG_SIZE-1));
        wakeup_write_index += write_index;
    }

    ccprintf("====== wakeup log [%02x] -> [%x]\n", (uint16_t)data, wakeup_write_index);

    // add timestamp
    log_Data.log_timestamp = NPCX_TTC;
    log_Data.log_id = data;
    
    // write wakeup cause
    if(flash_write(wakeup_write_index, LOG_SIZE, (const char *)(&log_Data)))
    {
        return;
    }

    // write page full flag
    wakeup_write_index += LOG_SIZE;
    if(!(wakeup_write_index&(DATA_PAGE_SIZE-1))) // 128-byte page full
    {
        full_flag[0] = 0xAA;
        page_index = ((wakeup_write_index - base_address)/DATA_PAGE_SIZE);

        ccprintf("====== wakeup page full, page_index = [%x]\n", page_index);
        
        flash_write(WAKEUP_HEADER_OFFSET+page_index, 1, full_flag);
    }

    // All the pages are full
    if(WAKEUP_RANGE_END == wakeup_write_index)
    {
        ccprintf("====== wakeup range full, wakeup_write_index=[%x] erease start[%x] size[%x]\n",
                    wakeup_write_index, WAKEUP_RANGE_START, WAKEUP_RANGE_SIZE);

        // read last 4
        flash_read((WAKEUP_RANGE_END-(4*LOG_SIZE)), (4*LOG_SIZE), (char *)eFlash_Data);

        // erase 4K
        if(eflash_debug_physical_erase(WAKEUP_RANGE_START, WAKEUP_RANGE_SIZE))
        {
            wakeup_write_index = 0;
        }
        else
        {
            wakeup_write_index = WAKEUP_DATA_OFFSET;
            // write last 4
            flash_write(wakeup_write_index, (4*LOG_SIZE), (char *)eFlash_Data);
            wakeup_write_index = WAKEUP_DATA_OFFSET+(4*LOG_SIZE);
        }
    }
}

/* Switch the latest ID before */
static void update_cause_ram_ags(uint32_t *data, uint32_t size)
{
    uint8_t i, tmp = 0;
    uint32_t eFlash_Data[8] = {0};

    if (size > 0x08) {
        return;
    }

    for (i = 0; i < size; ) {
        if (*(data + i) > 0) {
            tmp++;
        }
        i += 2;
    }
    for (i = 0; i < size; i++) {
        eFlash_Data[i] = *(data + i);
    }
    if (tmp == size/2) {
        *(data + 0) = eFlash_Data[6];
        *(data + 1) = eFlash_Data[7];
        *(data + 2) = eFlash_Data[4];
        *(data + 3) = eFlash_Data[5];
        *(data + 4) = eFlash_Data[2];
        *(data + 5) = eFlash_Data[3];
        *(data + 6) = eFlash_Data[0];
        *(data + 7) = eFlash_Data[1];
    } else if(tmp == size/2 - 1) {
        *(data + 0) = eFlash_Data[4];
        *(data + 1) = eFlash_Data[5];
        *(data + 2) = eFlash_Data[2];
        *(data + 3) = eFlash_Data[3];
        *(data + 4) = eFlash_Data[0];
        *(data + 5) = eFlash_Data[1];
        *(data + 6) = 0;
        *(data + 7) = 0;
    } else if(tmp == size/2 - 2) {
        *(data + 0) = eFlash_Data[2];
        *(data + 1) = eFlash_Data[3];
        *(data + 2) = eFlash_Data[0];
        *(data + 3) = eFlash_Data[1];
        *(data + 4) = 0;
        *(data + 5) = 0;
        *(data + 6) = 0;
        *(data + 7) = 0;
    } else if(tmp == size/2 -3) {
        *(data + 0) = eFlash_Data[0];
        *(data + 1) = eFlash_Data[1];
        *(data + 2) = 0;
        *(data + 3) = 0;
        *(data + 4) = 0;
        *(data + 5) = 0;
        *(data + 6) = 0;
        *(data + 7) = 0;
    }
}

static void abnormalPowerDownTimes(void)
{
    uint32_t *mptr = (uint32_t *)host_get_memmap(EC_MEMMAP_SHUTDOWN_CAUSE);
    uint32_t regVal = 0x0;

    regVal = *mptr & 0xFF;
    if (regVal == (LOG_ID_SHUTDOWN_0xFC & 0xFF)) {
        regVal = *(mptr + 2) & 0xFF;
        if (regVal == (LOG_ID_SHUTDOWN_0x08 & 0xFF)) {
            g_abnormalPowerDownTimes++;
            mfg_data_write(MFG_ABNORMAL_POWER_DOWN_TIMES_OFFSET, g_abnormalPowerDownTimes);
        } else {
            g_abnormalPowerDownTimes = 0;
            mfg_data_write(MFG_ABNORMAL_POWER_DOWN_TIMES_OFFSET, g_abnormalPowerDownTimes);
        }
    } else {
        if (regVal == (LOG_ID_SHUTDOWN_0x08 & 0xFF)) {
            g_abnormalPowerDownTimes++;
            mfg_data_write(MFG_ABNORMAL_POWER_DOWN_TIMES_OFFSET, g_abnormalPowerDownTimes);
        } else {
            g_abnormalPowerDownTimes = 0;
            mfg_data_write(MFG_ABNORMAL_POWER_DOWN_TIMES_OFFSET, g_abnormalPowerDownTimes);
        }
    }
}

uint8_t getAbnormalPowerDownTimes(void)
{
    ccprintf("get abnormal power down times: %d\n", g_abnormalPowerDownTimes);
    return g_abnormalPowerDownTimes;
}
void clearAbnormalPowerDownTimes(void)
{
    g_abnormalPowerDownTimes = 0;
    mfg_data_write(MFG_ABNORMAL_POWER_DOWN_TIMES_OFFSET, g_abnormalPowerDownTimes);
    ccprintf("clear abnormal power down times\n");
}

static void update_cause_ram(void)
{
    uint32_t eFlash_Data[8]={0};
    uint32_t i;
    int32_t tmp = 0;
    uint32_t *mptr = NULL;
    int status = EC_SUCCESS;
	uint32_t shutdown_write_index_curr = 0;
	uint32_t shutdown_write_index_align_log = 0;
	struct ec_params_flash_log *log_Data = NULL;

    eflash_debug_init();
    /* update shutdown cause to ram */
    for (i = 0; i < LOG_SIZE; i++) {
        eFlash_Data[i] = 0;
    }
    mptr = (uint32_t *)host_get_memmap(EC_MEMMAP_SHUTDOWN_CAUSE);

	shutdown_write_index_curr = shutdown_write_index;

    if(shutdown_write_index_curr & (LOG_SIZE-1)) {
		ccprintf("====== chipset resume, shutdown index(0x%08x), report aligned data\n", shutdown_write_index_curr);
		shutdown_write_index_align_log = LOG_SIZE - (shutdown_write_index_curr&(LOG_SIZE-1));
		shutdown_write_index_curr += shutdown_write_index_align_log;
	}

    if(shutdown_write_index_curr > (SHUTDOWN_HEADER_OFFSET + DATA_PAGE_SIZE + 4*LOG_SIZE)) {
        // read last 4
        status = flash_read((shutdown_write_index_curr-(4*LOG_SIZE)), (4*LOG_SIZE), (char *)eFlash_Data);
        if (status == EC_SUCCESS) {
			if(shutdown_write_index_align_log) {
				log_Data = (struct ec_params_flash_log *)(&eFlash_Data[LOG_SIZE -2]);
				log_Data->log_timestamp = 1;
				log_Data->log_id = LOG_ID_SHUTDOWN_0x08;
			}
            for (i = 0; i < LOG_SIZE; i++) {
                *(mptr+i) = eFlash_Data[i];
            }
        }
    } else {
        tmp = shutdown_write_index_curr - SHUTDOWN_HEADER_OFFSET - DATA_PAGE_SIZE;
        if ((tmp > 0) && (tmp < (LOG_SIZE * 4 + 1))) {
            status = flash_read((SHUTDOWN_HEADER_OFFSET + DATA_PAGE_SIZE), tmp, (char *)eFlash_Data);
            if (status == EC_SUCCESS) {
				if(shutdown_write_index_align_log) {
					log_Data = (struct ec_params_flash_log *)(&eFlash_Data[tmp / 4 -2]);
					log_Data->log_timestamp = 1;
					log_Data->log_id = LOG_ID_SHUTDOWN_0x08;
				}
                for (i = 0; i < tmp / 4; i++) {
                    *(mptr+i) = eFlash_Data[i];
                }
            }
        }
    }
    update_cause_ram_ags((uint32_t *)mptr, LOG_SIZE);

    /* update wakeup cause to ram */
    for (i = 0; i < LOG_SIZE; i++) {
        eFlash_Data[i] = 0;
    }
    mptr = (uint32_t *)host_get_memmap(EC_MEMMAP_WAKEUP_CAUSE);
    if(wakeup_write_index > (WAKEUP_HEADER_OFFSET + DATA_PAGE_SIZE + 4*LOG_SIZE)) {
        // read last 4
        status = flash_read((wakeup_write_index-(4*LOG_SIZE)), (4*LOG_SIZE), (char *)eFlash_Data);
        if (status == EC_SUCCESS) {
            for (i = 0; i < LOG_SIZE; i++) {
                *(mptr+i) = eFlash_Data[i];
            }
        }
    } else {
        tmp = wakeup_write_index - WAKEUP_HEADER_OFFSET - DATA_PAGE_SIZE;
        if ((tmp > 0) && (tmp < (LOG_SIZE * 4 + 1))) {
            status = flash_read((WAKEUP_HEADER_OFFSET + DATA_PAGE_SIZE), tmp, (char *)eFlash_Data);
            if (status == EC_SUCCESS) {
                for (i = 0; i < tmp / 4; i++) {
                    *(mptr+i) = eFlash_Data[i];
                }
            }
        }
    }
    update_cause_ram_ags((uint32_t *)mptr, LOG_SIZE);

    abnormalPowerDownTimes();
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, update_cause_ram, HOOK_PRIO_DEFAULT);

static enum ec_status host_command_write_flash_log(struct host_cmd_handler_args *args)
{
    const struct ec_params_flash_log *p = args->params;

    ccprintf(" HOST write shutdown ID = [%x]\n", p->log_id);
    shutdown_cause_record(p->log_id);
    
    return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_LOG_SET_VALUE,
        host_command_write_flash_log,
        EC_VER_MASK(0));

static int console_command_write_flash_log(int argc, char **argv)
{
    struct ec_params_flash_log log_Data;
    
    if (argc == 3)
    {
        char *e;
        uint32_t t = strtoi(argv[2], &e, 0);
        if (*e)
            return EC_ERROR_PARAM2;

        log_Data.log_id = t;

        if(!strcasecmp(argv[1], "shutdown"))
        {
            shutdown_cause_record(log_Data.log_id);
        }
        else if(!strcasecmp(argv[1], "wakeup"))
        {
            wakeup_cause_record(log_Data.log_id);
        }
        else
        {
            return EC_ERROR_PARAM2;
        }
    }
    else if (argc > 1)
    {
        return EC_ERROR_INVAL;
    }

    cprintf(CC_COMMAND, "wakeup_write_index=%x shutdown_write_index=%x\n", 
                wakeup_write_index, shutdown_write_index);

    return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flash_log, console_command_write_flash_log,
        "[shutdown/wakeup <log_id>]",
        "Write log_id to flash");

/*******************************************************************************
* Some data needs to be saved in case of power loss, We defined 4K of space 
* to store the necessary data. 
* The offset position of the flash space is 0x3E000.
* The size is 4K(x01000).
*
*******************************************************************************/
#define MFG_DATA_ADDRESS    0x3E000
#define MFG_DATA_BLOCK_SIZE 0x1000  // 4K
#define MFG_DATA_SIZE       256     // 256-Byte

static uint8_t mfg_data_map[MFG_DATA_SIZE] __aligned(8);

static void mfg_data_sync_deferred(void)
{
    if(eflash_debug_physical_erase(MFG_DATA_ADDRESS, MFG_DATA_BLOCK_SIZE))
    {
        ccprintf(" mfg data update fail\n");
    }
    else
    {
        flash_write(MFG_DATA_ADDRESS, MFG_DATA_SIZE, (char *)mfg_data_map);
        ccprintf(" mfg data update OK\n");
    }
}
DECLARE_DEFERRED(mfg_data_sync_deferred);

#define FLASH_SYNC_DEBOUNCE_US  (30 * MSEC)
void mfg_data_write(uint8_t index, uint8_t data)
{
    uint8_t *mfgMode = NULL;
    
    if(index >= MFG_OFFSET_COUNT)
    {
        return;
    }

    if(MFG_MODE_OFFSET == index) {
        mfgMode = host_get_memmap(EC_MEMMAP_MFG_MODE);
        *mfgMode = data;    /* sync to EC RAM*/
    } else if(MFG_AC_RECOVERY_OFFSET == index) {
        mfgMode = host_get_memmap(EC_MEMMAP_AC_RECOVERY);
        *mfgMode = data;    /* sync to EC RAM*/
    } else if(MFG_ABNORMAL_POWER_DOWN_TIMES_OFFSET == index) {
    } else if(MFG_CHASSIS_INTRUSION_DATA_OFFSET == index) {
    } else if(MFG_CHASSIS_INTRUSION_MODE_OFFSET == index) {
    } else if(MFG_POWER_LAST_STATE_OFFSET == index) {
    } else if(MFG_POWER_LAN_WAKE_OFFSET == index) {
    } else if(MFG_POWER_WLAN_WAKE_OFFSET == index) {
    } else {
        return;
    }

    mfg_data_map[index] = data;
    hook_call_deferred(&mfg_data_sync_deferred_data, FLASH_SYNC_DEBOUNCE_US);
}

uint8_t mfg_data_read(uint8_t index)
{
    if(index >= MFG_OFFSET_COUNT)
    {
        return 0;
    }
    
    ccprintf(" mfg data read OK, index=[0x%02x] data=[0x%02x]\n",
                index, mfg_data_map[index]);
    return mfg_data_map[index];
}

static void mfg_data_init(void)
{
    uint8_t *mfgMode;
    
    flash_read(MFG_DATA_ADDRESS, MFG_DATA_SIZE, (char *)mfg_data_map);
    
    /* initialize read MFG mode */
    mfgMode = host_get_memmap(EC_MEMMAP_MFG_MODE);
    *mfgMode = mfg_data_map[MFG_MODE_OFFSET];

    /* initialize read AC recovery state */
    mfgMode = host_get_memmap(EC_MEMMAP_AC_RECOVERY);
    if (0xFF == mfg_data_map[MFG_AC_RECOVERY_OFFSET]) {
        mfg_data_write(MFG_AC_RECOVERY_OFFSET, 0x01); /* set default data */
        *mfgMode = 0x01; /* default is AC recovery to power on */
    } else {
        *mfgMode = mfg_data_map[MFG_AC_RECOVERY_OFFSET];
    }

    /* initialize chassis Intrusion data */
    *mfgMode = mfg_data_map[MFG_CHASSIS_INTRUSION_DATA_OFFSET];
    set_chassisIntrusion_data(*mfgMode);

    /* initialize abnormal power on shutdown ID data */
    *mfgMode = mfg_data_map[MFG_ABNORMAL_POWER_DOWN_TIMES_OFFSET];
    if (0xFF == *mfgMode) {
        mfg_data_write(MFG_ABNORMAL_POWER_DOWN_TIMES_OFFSET, 0x00); /* set default data */
        g_abnormalPowerDownTimes = 0;
    } else {
        g_abnormalPowerDownTimes = *mfgMode;
    }

#ifdef CONFIG_LAN_WAKE_SWITCH
    /* initialize LAN WLAN enable data */
    mfgMode = host_get_memmap(EC_MEMMAP_SYS_MISC2);
    if (EC_GENERAL_SIGNES == mfg_data_map[MFG_POWER_LAN_WAKE_OFFSET]) {
        (*mfgMode) |= EC_MEMMAP_POWER_LAN_WAKE;
    } else {
        (*mfgMode) &= (~EC_MEMMAP_POWER_LAN_WAKE);
    }
    if (EC_GENERAL_SIGNES == mfg_data_map[MFG_POWER_WLAN_WAKE_OFFSET]) {
        (*mfgMode) |= EC_MEMMAP_POWER_WLAN_WAKE;
    } else {
        (*mfgMode) &= (~EC_MEMMAP_POWER_WLAN_WAKE);
    }
#endif

    /* Check MFG MODE when it isn't MFG MODE, force enable MFG MODE */
#ifdef CONFIG_MFG_FACTORY_MODE
    if (0xBE == mfg_data_map[MFG_MODE_OFFSET]) {
        mfgMode = host_get_memmap(EC_MEMMAP_MFG_MODE);
        *mfgMode = 0xFF;
        mfg_data_write(MFG_MODE_OFFSET, 0xFF);
    }
#endif
}
DECLARE_HOOK(HOOK_INIT, mfg_data_init, HOOK_PRIO_DEFAULT);

static int console_command_mfg_data(int argc, char **argv)
{
    if (1 == argc)
    {
        return EC_ERROR_INVAL;
    }
    else
    {
        char *e;
        uint8_t d;
        uint8_t index;

        if(!strcasecmp(argv[1], "write") && (4==argc))
        {
            index = strtoi(argv[2], &e, 0);
            if (*e)
                return EC_ERROR_PARAM2;
        
            d = strtoi(argv[3], &e, 0);
            if (*e)
                return EC_ERROR_PARAM2;
            mfg_data_write(index, d);
        }
        else if(!strcasecmp(argv[1], "read") && (3==argc))
        {
            index = strtoi(argv[2], &e, 0);
            if (*e)
                return EC_ERROR_PARAM2;
            
            mfg_data_read(index);
        }
        else if(!strcasecmp(argv[1], "show"))
        {
            /* TODO, Maybe you need to parse the MFG data here */
            ccprintf("MFG data : \n");
            for(d=0; d<16; d++)
            {
                ccprintf("0x%X ", mfg_data_map[d]);
            }
            /* default 0xFF is MFG mode enable */
            ccprintf("\nMFG Mode    : %s\n",
                (0xFF==mfg_data_map[MFG_MODE_OFFSET])?("Enable"):("Disable"));

            /* AC recovery state, 1:on, 2:off, 3:pre */
            if (0x01==mfg_data_map[MFG_AC_RECOVERY_OFFSET]) {
                ccprintf("AC Recovery : on\n");
            }
            else if (0x02==mfg_data_map[MFG_AC_RECOVERY_OFFSET]) {
                ccprintf("AC Recovery : off\n");
            }
            else if (0x03==mfg_data_map[MFG_AC_RECOVERY_OFFSET]) {
                ccprintf("AC Recovery : previous\n");
            } else {
                ccprintf("AC Recovery : unknown\n");
            }
        }
        else
        {
            return EC_ERROR_PARAM2;
        }
    }
    
    return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(mfg_data, console_command_mfg_data,
        "[show]"
        "[read <index>]"
        "[write <index> <data>]",
        "read/Write mfg data to flash");
#else
void shutdown_cause_record(uint32_t data)
{
    ccprintf(" Please define CONFIG_FLASH_LOG_OEM\n");
}

void wakeup_cause_record(uint32_t data)
{
    ccprintf(" Please define CONFIG_FLASH_LOG_OEM\n");
}

void mfg_data_write(uint8_t index, uint8_t data)
{
    ccprintf(" Please define CONFIG_FLASH_LOG_OEM\n");
}

uint8_t mfg_data_read(uint8_t index)
{
    ccprintf(" Please define CONFIG_FLASH_LOG_OEM\n");
    return 0;
}
#endif
