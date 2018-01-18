/*
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "libusb.h"

#include <time.h>
#define msleep(msecs) nanosleep( \
		&(struct timespec) \
		{ msecs / 1000, (msecs * 1000000) % 1000000000UL},\
	   	NULL);

#if !defined(bool)
#define bool int
#endif
#if !defined(true)
#define true (1 == 1)
#endif
#if !defined(false)
#define false (!true)
#endif

// Future versions of libusb will use usb_interface instead of interface
// in libusb_config_descriptor => catter for that
#define usb_interface interface

// Global variables
static bool binary_dump = false;
static bool extra_info = false;
static bool force_device_request = false;	// For WCID descriptor queries
static const char* binary_name = NULL;

static int perr(char const *format, ...)
{
	va_list args;
	int r;

	va_start (args, format);
	r = vfprintf(stderr, format, args);
	va_end(args);

	return r;
}

#define ERR_EXIT(errcode) do { perr("   %s\n", libusb_strerror((enum libusb_error)errcode)); return -1; } while (0)
#define CALL_CHECK(fcall) do { r=fcall; if (r < 0) ERR_EXIT(r); } while (0);
#define B(x) (((x)!=0)?1:0)
#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])

#define RETRY_MAX                     5
#define REQUEST_SENSE_LENGTH          0x12
#define INQUIRY_LENGTH                0x24

// Section 5.1: Command Block Wrapper (CBW)
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

// Section 5.2: Command Status Wrapper (CSW)
struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

static uint8_t cdb_length[256] = {
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,02,00,  //  F
};

static uint16_t VID, PID;

static void display_buffer_hex(unsigned char *buffer, unsigned size)
{
	unsigned i, j, k;

	for (i=0; i<size; i+=16) {
		printf("\n  %08x  ", i);
		for(j=0,k=0; k<16; j++,k++) {
			if (i+j < size) {
				printf("%02x", buffer[i+j]);
			} else {
				printf("  ");
			}
			printf(" ");
		}
		printf(" ");
		for(j=0,k=0; k<16; j++,k++) {
			if (i+j < size) {
				if ((buffer[i+j] < 32) || (buffer[i+j] > 126)) {
					printf(".");
				} else {
					printf("%c", buffer[i+j]);
				}
			}
		}
	}
	printf("\n" );
}

static int send_mass_storage_command(libusb_device_handle *handle, uint8_t endpoint, 
	uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int i, r, size;
	struct command_block_wrapper cbw;

	if (cdb == NULL) {
		return -1;
	}

	if (endpoint & LIBUSB_ENDPOINT_IN) {
		perr("send_mass_storage_command: cannot send command on IN endpoint\n");
		return -1;
	}

	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw.CBWCB))) {
		perr("send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",
			cdb[0], cdb_len);
		return -1;
	}
	if (cdb[0] == 0xFE) /* vendor */
		if (cdb[1] == 2) /* special command length*/
			cdb_len = 6;

	memset(&cbw, 0, sizeof(cbw));
	cbw.dCBWSignature[0] = 'U';
	cbw.dCBWSignature[1] = 'S';
	cbw.dCBWSignature[2] = 'B';
	cbw.dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw.dCBWTag = tag++;
	cbw.dCBWDataTransferLength = data_length;
	cbw.bmCBWFlags = direction;
	cbw.bCBWLUN = 0;
	// Subclass is 1 or 6 => cdb_len
	cbw.bCBWCBLength = cdb_len;
	memcpy(cbw.CBWCB, cdb, cdb_len);

	i = 0;
	do {
		// The transfer length must always be exactly 31 bytes.
		r = libusb_bulk_transfer(handle, endpoint, (unsigned char*)&cbw, 31, &size, 1000);
		if (r == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt(handle, endpoint);
		}
		i++;
	} while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
	if (r != LIBUSB_SUCCESS) {
		perr("   send_mass_storage_command: %s\n", libusb_strerror((enum libusb_error)r));
		return -1;
	}

	printf("   sent %d CDB bytes\n", cdb_len);
	return 0;
}

static int get_mass_storage_status(libusb_device_handle *handle, uint8_t endpoint, uint32_t expected_tag)
{
	int i, r, size;
	struct command_status_wrapper csw;

	// The device is allowed to STALL this transfer. If it does, you have to
	// clear the stall and try again.
	i = 0;
	do {
		r = libusb_bulk_transfer(handle, endpoint, (unsigned char*)&csw, 13, &size, 1000);
		if (r == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt(handle, endpoint);
		}
		i++;
	} while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
	if (r != LIBUSB_SUCCESS) {
		perr("   get_mass_storage_status: %s\n", libusb_strerror((enum libusb_error)r));
		return -1;
	}
	if (size != 13) {
		perr("   get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}
	if (csw.dCSWTag != expected_tag) {
		perr("   get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw.dCSWTag);
		return -1;
	}
	// For this test, we ignore the dCSWSignature check for validity...
	printf("   Mass Storage Status: %02X (%s)\n", csw.bCSWStatus, csw.bCSWStatus?"FAILED":"Success");
	if (csw.dCSWTag != expected_tag)
		return -1;
	if (csw.bCSWStatus) {
		// REQUEST SENSE is appropriate only if bCSWStatus is 1, meaning that the
		// command failed somehow.  Larger values (2 in particular) mean that
		// the command couldn't be understood.
		if (csw.bCSWStatus == 1) {
			printf("CSW DataResidue:%d\n", csw.dCSWDataResidue);
			return -2;	// request Get Sense
		}
		else
			return -1;
	}

	// In theory we also should check dCSWDataResidue.  But lots of devices
	// set it wrongly.
	return 0;
}

static void printBufferHex(uint8_t *buffer, int size)
{
	int i;
	int col = 0;
	if ((buffer == NULL) || size <= 0)
		return;
    if (size > 256)
		return;

	printf("--------------------------------------------------------\n");
    printf("    00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
    printf("--------------------------------------------------------");
	for (i = 0; i < size; i++) {
		if (i % 16)
			printf(" ");
		else {
			printf("\n%02X| ", col);
			col+=0x10;
		}
		printf("%02X", buffer[i]);
	}
	printf("\n");
}

static int inquiry_smartbend(libusb_device_handle *handle, uint8_t endpoint_in, uint8_t endpoint_out)
{
	int r;
	int size;
	int i;
	uint32_t expected_tag;
	uint8_t buffer[64];
	uint8_t cdb[16];
	char vid[9], pid[9], rev[5];
	char mode[9];

	memset(buffer, 0, sizeof(buffer));
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x12;	// Inquiry
	cdb[4] = INQUIRY_LENGTH;

	send_mass_storage_command(handle, endpoint_out, cdb, LIBUSB_ENDPOINT_IN, INQUIRY_LENGTH, &expected_tag);
	CALL_CHECK(libusb_bulk_transfer(handle, endpoint_in, (unsigned char*)&buffer, INQUIRY_LENGTH, &size, 1000));
	printf("   received %d bytes\n", size);
	// The following strings are not zero terminated
	for (i=0; i<8; i++) {
		vid[i] = buffer[8+i];
		pid[i] = buffer[16+i];
		rev[i/2] = buffer[32+i/2];	// instead of another loop
	}
	vid[8] = 0;
	pid[8] = 0;
	rev[4] = 0;
    for (i=0; i < 8; i++) {
	   mode[i] = 0;
	}	   
	for (i=0; i < 3; i++) {
	  mode[i] = buffer[27+i];
	}
	printf("   Vendor:Product:Revision \"%8s\":\"%8s\":\"%4s\"\n", vid, pid, rev);
	printf("   Mode:%4s\n", mode);
	get_mass_storage_status(handle, endpoint_in, expected_tag);

	return 0;
}

static int readSettings_smartbend(libusb_device_handle *handle, uint8_t endpoint_in, uint8_t endpoint_out, uint8_t *outb, uint32_t len)
{
	int r;
	int size;
	int i;
	uint32_t expected_tag;
	uint8_t buffer[256];
	uint8_t cdb[16];
	char name[17];

	printf("Read Settings:\n");
	memset(buffer, 0, sizeof(buffer));
	memset(cdb, 0, sizeof(cdb));
	memset(name, 0, sizeof(name));
	cdb[0] = 0xFE;	// Vendor command
	cdb[1] = 0;     // Read Settings
	send_mass_storage_command(handle, endpoint_out, cdb, LIBUSB_ENDPOINT_IN, 256, &expected_tag);
	CALL_CHECK(libusb_bulk_transfer(handle, endpoint_in, (unsigned char*)&buffer, 256, &size, 1000));
	for (i = 0; i < 16; i++) {
		name[i] = buffer[i];
		if (name[i] == 0)
			break;
	}
	printf("   Name:%s, Received size: %d\n", name, size);
	printf("   Year:%d, Month: %d, Day:%d\n", buffer[0x10]+2000, buffer[0x11]+0, buffer[0x12]+0);
	printf("   Hour:%d, Minute: %d, Second:%d\n", buffer[0x13], buffer[0x14], buffer[0x15]);
	printf("   Display mode:%s\n", (buffer[0x1E]==0) ? "24Hrs" : "12Hrs");
	printf("   Control:%02x\n", buffer[0xA0]);
	printf("   Sleep monitor start: %02dh:%02d\n", buffer[0xA1], buffer[0xA2]);
	printf("   Sleep monitor stop: %02dh:%02d\n", buffer[0xA3], buffer[0xA4]);
	printf("   Alram: %02dh:%02d\n", buffer[0xA5], buffer[0xA6]);
	printf("   Smart LED: %s\n", (buffer[0xC1]==0) ? "disabled" : "enabled");
	printf("   Height:%3dcm, Weight %3.2fKg, Stride:%3dcm\n", buffer[0xC3]*256+buffer[0xC4],
			(buffer[0xC5]*256 + buffer[0xC6])/100.0f, buffer[0xC7]*256+buffer[0xC8]);
	get_mass_storage_status(handle, endpoint_in, expected_tag);
	printBufferHex(buffer, sizeof(buffer));
	if ((outb != 0) && (len >= 256)) {
		memcpy(outb, buffer, 256);
	}
	return 0;
}

static int writeSettings_smartbend(libusb_device_handle *handle, uint8_t endpoint_in, uint8_t endpoint_out, uint8_t *outb, uint32_t len)
{
	int r;
	int size;
	int i;
	uint32_t expected_tag;
	uint8_t cdb[16];

	printf("write Settings:\n");
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0xFE;	// Vendor command
	cdb[1] = 0x01;     // Write Settings
	size = 0;
	if ((outb != NULL) && (len >= 256)) {
	    send_mass_storage_command(handle, endpoint_out, cdb, 0, 256, &expected_tag);
	    CALL_CHECK(libusb_bulk_transfer(handle, endpoint_out, outb, 256, &size, 5000));
	    get_mass_storage_status(handle, endpoint_in, expected_tag);
	}
	return 0;
}

static int readSteps_smartbend(libusb_device_handle *handle, uint8_t endpoint_in, uint8_t endpoint_out,
		uint8_t page)
{
	int r;
	int size;
	int i;
	uint32_t expected_tag;
	uint8_t buffer[512];
	uint8_t cdb[16];

	printf("Read Steps: %d\n", 0+page);
	memset(buffer, 0, sizeof(buffer));
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0xFE;	// Vendor command
	cdb[1] = 0x02;  // Read Steps
	cdb[4] = page;
	send_mass_storage_command(handle, endpoint_out, cdb, LIBUSB_ENDPOINT_IN, 512, &expected_tag);
	CALL_CHECK(libusb_bulk_transfer(handle, endpoint_in, (unsigned char*)&buffer, 512, &size, 1000));
	printf("   Received size: %d\n", size);
	printf("   Year:%d, Month: %d, Day:%d, Hour:%d\n",
			buffer[0x00]+2000, buffer[0x01]+0, buffer[0x02]+0, buffer[0x03]);
	for(i = 0; i < 60; i+=2) {
		printf("%02dMinu, Steps: %d\n", i, buffer[i+4]*256+buffer[i+5]);
	}
	get_mass_storage_status(handle, endpoint_in, expected_tag);
}

// Mass Storage device to test bulk transfers (non destructive test)
static int test_smartbend(libusb_device_handle *handle, uint8_t endpoint_in, uint8_t endpoint_out)
{
	uint8_t settings[256];
	memset(settings, 0, sizeof(settings));
	// Send Inquiry
	inquiry_smartbend(handle, endpoint_in, endpoint_out);
	// Read Settings
	readSettings_smartbend(handle, endpoint_in, endpoint_out, settings, sizeof(settings));
	// to modify sleep minute for check write functions
	settings[0xA2] = settings[0xA2] ? 0 : 20;
	writeSettings_smartbend(handle, endpoint_in, endpoint_out, settings, sizeof(settings));
	// read from device to write correct 
 	readSettings_smartbend(handle, endpoint_in, endpoint_out, settings, sizeof(settings));
	// Read Steps
	readSteps_smartbend(handle, endpoint_in, endpoint_out, 0x60);
	return 0;
}

// Read the MS WinUSB Feature Descriptors, that are used on Windows 8 for automated driver installation
static void read_ms_winsub_feature_descriptors(libusb_device_handle *handle, uint8_t bRequest, int iface_number)
{
#define MAX_OS_FD_LENGTH 256
	int i, r;
	uint8_t os_desc[MAX_OS_FD_LENGTH];
	uint32_t length;
	void* le_type_punning_IS_fine;
	struct {
		const char* desc;
		uint8_t recipient;
		uint16_t index;
		uint16_t header_size;
	} os_fd[2] = {
		{"Extended Compat ID", LIBUSB_RECIPIENT_DEVICE, 0x0004, 0x10},
		{"Extended Properties", LIBUSB_RECIPIENT_INTERFACE, 0x0005, 0x0A}
	};

	if (iface_number < 0) return;
	// WinUSB has a limitation that forces wIndex to the interface number when issuing
	// an Interface Request. To work around that, we can force a Device Request for
	// the Extended Properties, assuming the device answers both equally.
	if (force_device_request)
		os_fd[1].recipient = LIBUSB_RECIPIENT_DEVICE;

	for (i=0; i<2; i++) {
		printf("\nReading %s OS Feature Descriptor (wIndex = 0x%04d):\n", os_fd[i].desc, os_fd[i].index);

		// Read the header part
		r = libusb_control_transfer(handle, (uint8_t)(LIBUSB_ENDPOINT_IN|LIBUSB_REQUEST_TYPE_VENDOR|os_fd[i].recipient),
			bRequest, (uint16_t)(((iface_number)<< 8)|0x00), os_fd[i].index, os_desc, os_fd[i].header_size, 1000);
		if (r < os_fd[i].header_size) {
			perr("   Failed: %s", (r<0)?libusb_strerror((enum libusb_error)r):"header size is too small");
			return;
		}
		le_type_punning_IS_fine = (void*)os_desc;
		length = *((uint32_t*)le_type_punning_IS_fine);
		if (length > MAX_OS_FD_LENGTH) {
			length = MAX_OS_FD_LENGTH;
		}

		// Read the full feature descriptor
		r = libusb_control_transfer(handle, (uint8_t)(LIBUSB_ENDPOINT_IN|LIBUSB_REQUEST_TYPE_VENDOR|os_fd[i].recipient),
			bRequest, (uint16_t)(((iface_number)<< 8)|0x00), os_fd[i].index, os_desc, (uint16_t)length, 1000);
		if (r < 0) {
			perr("   Failed: %s", libusb_strerror((enum libusb_error)r));
			return;
		} else {
			display_buffer_hex(os_desc, r);
		}
	}
}

static int test_device(uint16_t vid, uint16_t pid)
{
	libusb_device_handle *handle;
	libusb_device *dev;
	uint8_t bus, port_path[8];
	struct libusb_config_descriptor *conf_desc;
	const struct libusb_endpoint_descriptor *endpoint;
	int i, j, k, r;
	int iface, nb_ifaces, first_iface = -1;
	struct libusb_device_descriptor dev_desc;
	const char* speed_name[5] = { "Unknown", "1.5 Mbit/s (USB LowSpeed)", "12 Mbit/s (USB FullSpeed)",
		"480 Mbit/s (USB HighSpeed)", "5000 Mbit/s (USB SuperSpeed)"};
	char string[128];
	uint8_t string_index[3];	// indexes of the string descriptors
	uint8_t endpoint_in = 0, endpoint_out = 0;	// default IN and OUT endpoints

	printf("Opening device %04X:%04X...\n", vid, pid);
	handle = libusb_open_device_with_vid_pid(NULL, vid, pid);

	if (handle == NULL) {
		perr("  Failed.\n");
		return -1;
	}

	dev = libusb_get_device(handle);
	bus = libusb_get_bus_number(dev);
	if (extra_info) {
		r = libusb_get_port_numbers(dev, port_path, sizeof(port_path));
		if (r > 0) {
			printf("\nDevice properties:\n");
			printf("        bus number: %d\n", bus);
			printf("         port path: %d", port_path[0]);
			for (i=1; i<r; i++) {
				printf("->%d", port_path[i]);
			}
			printf(" (from root hub)\n");
		}
		r = libusb_get_device_speed(dev);
		if ((r<0) || (r>4)) r=0;
		printf("             speed: %s\n", speed_name[r]);
	}

	printf("\nReading device descriptor:\n");
	CALL_CHECK(libusb_get_device_descriptor(dev, &dev_desc));
	printf("            length: %d\n", dev_desc.bLength);
	printf("      device class: %d\n", dev_desc.bDeviceClass);
	printf("               S/N: %d\n", dev_desc.iSerialNumber);
	printf("           VID:PID: %04X:%04X\n", dev_desc.idVendor, dev_desc.idProduct);
	printf("         bcdDevice: %04X\n", dev_desc.bcdDevice);
	printf("   iMan:iProd:iSer: %d:%d:%d\n", dev_desc.iManufacturer, dev_desc.iProduct, dev_desc.iSerialNumber);
	printf("          nb confs: %d\n", dev_desc.bNumConfigurations);
	// Copy the string descriptors for easier parsing
	string_index[0] = dev_desc.iManufacturer;
	string_index[1] = dev_desc.iProduct;
	string_index[2] = dev_desc.iSerialNumber;

	printf("\nReading first configuration descriptor:\n");
	CALL_CHECK(libusb_get_config_descriptor(dev, 0, &conf_desc));
	nb_ifaces = conf_desc->bNumInterfaces;
	printf("             nb interfaces: %d\n", nb_ifaces);
	if (nb_ifaces > 0)
		first_iface = conf_desc->usb_interface[0].altsetting[0].bInterfaceNumber;
	for (i=0; i<nb_ifaces; i++) {
		printf("              interface[%d]: id = %d\n", i,
			conf_desc->usb_interface[i].altsetting[0].bInterfaceNumber);
		for (j=0; j<conf_desc->usb_interface[i].num_altsetting; j++) {
			printf("interface[%d].altsetting[%d]: num endpoints = %d\n",
				i, j, conf_desc->usb_interface[i].altsetting[j].bNumEndpoints);
			printf("   Class.SubClass.Protocol: %02X.%02X.%02X\n",
				conf_desc->usb_interface[i].altsetting[j].bInterfaceClass,
				conf_desc->usb_interface[i].altsetting[j].bInterfaceSubClass,
				conf_desc->usb_interface[i].altsetting[j].bInterfaceProtocol);

			for (k=0; k<conf_desc->usb_interface[i].altsetting[j].bNumEndpoints; k++) {
				struct libusb_ss_endpoint_companion_descriptor *ep_comp = NULL;
				endpoint = &conf_desc->usb_interface[i].altsetting[j].endpoint[k];
				printf("       endpoint[%d].address: %02X\n", k, endpoint->bEndpointAddress);
				// Use the first interrupt or bulk IN/OUT endpoints as default for testing
				if ((endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) & (LIBUSB_TRANSFER_TYPE_BULK | LIBUSB_TRANSFER_TYPE_INTERRUPT)) {
					if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
						if (!endpoint_in)
							endpoint_in = endpoint->bEndpointAddress;
					} else {
						if (!endpoint_out)
							endpoint_out = endpoint->bEndpointAddress;
					}
				}
				printf("           max packet size: %04X\n", endpoint->wMaxPacketSize);
				printf("          polling interval: %02X\n", endpoint->bInterval);
				libusb_get_ss_endpoint_companion_descriptor(NULL, endpoint, &ep_comp);
				if (ep_comp) {
					printf("                 max burst: %02X   (USB 3.0)\n", ep_comp->bMaxBurst);
					printf("        bytes per interval: %04X (USB 3.0)\n", ep_comp->wBytesPerInterval);
					libusb_free_ss_endpoint_companion_descriptor(ep_comp);
				}
			}
		}
	}
	libusb_free_config_descriptor(conf_desc);

	libusb_set_auto_detach_kernel_driver(handle, 1);
	for (iface = 0; iface < nb_ifaces; iface++)
	{
		printf("\nClaiming interface %d...\n", iface);
		r = libusb_claim_interface(handle, iface);
		if (r != LIBUSB_SUCCESS) {
			perr("   Failed.\n");
		}
	}

	printf("\nReading string descriptors:\n");
	for (i=0; i<3; i++) {
		if (string_index[i] == 0) {
			continue;
		}
		if (libusb_get_string_descriptor_ascii(handle, string_index[i], (unsigned char*)string, 128) >= 0) {
			printf("   String (0x%02X): \"%s\"\n", string_index[i], string);
		}
	}

	CALL_CHECK(test_smartbend(handle, endpoint_in, endpoint_out));

	printf("\n");
	for (iface = 0; iface<nb_ifaces; iface++) {
		printf("Releasing interface %d...\n", iface);
		libusb_release_interface(handle, iface);
	}

	printf("Closing device...\n");
	libusb_close(handle);

	return 0;
}

int main(int argc, char** argv)
{
	bool show_help = false;
	bool debug_mode = false;
	const struct libusb_version* version;
	int j, r;
	size_t i, arglen;
	unsigned tmp_vid, tmp_pid;
	uint16_t endian_test = 0xBE00;
	char *error_lang = NULL, *old_dbg_str = NULL, str[256];

	// Default to generic, expecting VID:PID
	VID = 0x1B3F;
	PID = 0x30FE;
	
	if (((uint8_t*)&endian_test)[0] == 0xBE) {
		printf("Despite their natural superiority for end users, big endian\n"
			"CPUs are not supported with this program, sorry.\n");
		return 0;
	}

	if (argc >= 2) {
		for (j = 1; j<argc; j++) {
			arglen = strlen(argv[j]);
			if ( ((argv[j][0] == '-') || (argv[j][0] == '/'))
			  && (arglen >= 2) ) {
				switch(argv[j][1]) {
				case 'd':
					debug_mode = true;
					break;
				case 'i':
					extra_info = true;
					break;
				case 'w':
					force_device_request = true;
					break;
				case 'b':
					if ((j+1 >= argc) || (argv[j+1][0] == '-') || (argv[j+1][0] == '/')) {
						printf("   Option -b requires a file name\n");
						return 1;
					}
					binary_name = argv[++j];
					binary_dump = true;
					break;
				case 'l':
					if ((j+1 >= argc) || (argv[j+1][0] == '-') || (argv[j+1][0] == '/')) {
						printf("   Option -l requires an ISO 639-1 language parameter\n");
						return 1;
					}
					error_lang = argv[++j];
					break;
			default:
					show_help = true;
					break;
				}
			} else {
				for (i=0; i<arglen; i++) {
					if (argv[j][i] == ':')
						break;
				}
				if (i != arglen) {
					if (sscanf(argv[j], "%x:%x" , &tmp_vid, &tmp_pid) != 2) {
						printf("   Please specify VID & PID as \"vid:pid\" in hexadecimal format\n");
						return 1;
					}
					VID = (uint16_t)tmp_vid;
					PID = (uint16_t)tmp_pid;
				}
			}
		}
	}

	if (argc == 1) {
		VID = 0x1B3F;
		PID = 0x30FE;
	}

	if ((show_help) || (argc > 7)) {
		printf("usage: %s [-h] [-d] [-i] [-k] [-b file] [-l lang] [-x] [-w] [vid:pid]\n", argv[0]);
		printf("   -h      : display usage\n");
		printf("   -d      : enable debug output\n");
		printf("   -i      : print topology and speed info\n");
		printf("   -b file : dump Mass Storage data to file 'file'\n");
		printf("   -l lang : language to report errors in (ISO 639-1)\n");
		printf("   -w      : force the use of device requests when querying WCID descriptors\n");
		printf("If only the vid:pid is provided, xusb attempts to run the most appropriate test\n");
		return 0;
	}

	// xusb is commonly used as a debug tool, so it's convenient to have debug output during libusb_init(),
	// but since we can't call on libusb_set_debug() before libusb_init(), we use the env variable method
	old_dbg_str = getenv("LIBUSB_DEBUG");
	if (debug_mode) {
		if (putenv("LIBUSB_DEBUG=4") != 0)	// LIBUSB_LOG_LEVEL_DEBUG
			printf("Unable to set debug level");
	}

	version = libusb_get_version();
	printf("Using libusb v%d.%d.%d.%d\n\n", version->major, version->minor, version->micro, version->nano);
	r = libusb_init(NULL);
	if (r < 0)
		return r;

	// If not set externally, and no debug option was given, use info log level
	if ((old_dbg_str == NULL) && (!debug_mode))
		libusb_set_debug(NULL, LIBUSB_LOG_LEVEL_INFO);
	if (error_lang != NULL) {
		r = libusb_setlocale(error_lang);
		if (r < 0)
			printf("Invalid or unsupported locale '%s': %s\n", error_lang, libusb_strerror((enum libusb_error)r));
	}

	test_device(VID, PID);

	libusb_exit(NULL);

	if (debug_mode) {
		snprintf(str, sizeof(str), "LIBUSB_DEBUG=%s", (old_dbg_str == NULL)?"":old_dbg_str);
		str[sizeof(str) - 1] = 0;	// Windows may not NUL terminate the string
	}

	return 0;
}
