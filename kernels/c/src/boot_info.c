/* boot_info に関する補助関数 */
#include <kui/boot_info.h>

const char *mem_type_name(enum mem_type type)
{
	switch (type) {
	case MEM_USABLE:
		return "usable";
	case MEM_RESERVED:
		return "reserved";
	case MEM_ACPI_RECLAIMABLE:
		return "acpi-reclaimable";
	case MEM_ACPI_NVS:
		return "acpi-nvs";
	case MEM_BAD:
		return "bad";
	case MEM_BOOTLOADER_RECLAIMABLE:
		return "bootloader-reclaimable";
	case MEM_KERNEL_AND_MODULES:
		return "kernel-and-modules";
	case MEM_FRAMEBUFFER:
		return "framebuffer";
	case MEM_TYPE_COUNT:
		break;
	}
	return "unknown";
}
