// SST39SF040 FLASHER

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "rs232.h"

#include "flashdata.h"

static int COM_PORT = -1;

// Wait for "ready" status from the Arduino.
static void waitRDY(void) {
	static const char Sig[] = "RDY";
	uint8_t tempC, x;
	uint32_t junkC = 0;
	for (x = 0; x < 3; ++x) {
		do {
			RS232_PollComport(COM_PORT, &tempC, 1);
			if (tempC != Sig[x]) {
				junkC++;
				printf("Junk Char %d or %c while waiting for %c so far skipped %d\n", tempC, tempC, Sig[x], junkC);
			}
		} while (tempC != Sig[x]);
	}
	if (junkC != 0)
		printf("/n%d junk bytes skipped\n", junkC);
}

// Send a byte of data to the flash chip.
static void programByte(uint8_t dat) {
	uint8_t datr;
	RS232_SendByte(COM_PORT, dat);
	RS232_PollComport(COM_PORT, &datr, 1);
	if (datr != 'N')
		printf("ERROR: Programming byte letter code '%c' failed\n", datr);
}

// Show COM port list.
static void printCOM() {
	printf("\nCOM Port ID Table:\n");
	for (int i = 0; i < sizeof(comports) / sizeof(comports[0]); ++i)
		printf("\t %d %s\n", i, comports[i]);
}

// Show help info.
static void help(char** argv) {
	printf("Usage: %s COM_PORT_ID file_name [-d]\n", argv[0]);
	printf("-d is optional and it is used to dump the contents of the flash memory chip to the specified file.\n");
	// Print the comport IDs.
	printCOM();
}


// Main
int main(int argc, char** argv) {
	printf("\n------- SST FLASHER -------\n\n");

	// Determine flashing/dumping mode.
	int dump = 0;
	if (argc != 3 && argc != 4) {
		help(argv);
		return 1;
	} else if (argc == 4) {
		if (strcmp("-d", argv[3]) == 0)
			dump = 1;
		else {
			printf("To specify dumping you need to use -d but you did %s instead\nThis program will show help and exit\n", argv[3]);
			help(argv);
			return 1;
		}
	}
	
	// Assign COM port number.
	COM_PORT = strtoul(argv[1], NULL, 10);

	// Display info.
	if (dump) printf("\nDumping to ");
	else printf("Flashing from ");
	printf("%s on COM port %d\n", argv[2], COM_PORT);

	// Open COM port.
	if (RS232_OpenComport(COM_PORT, 500000)) {
		printf("ERROR: COM port %i could not be opened\n", COM_PORT);
		printCOM();
		return 1;
	}

	// Wait for RDY from the Arduino.
	waitRDY();

	//Get chip signature.
	RS232_SendByte(COM_PORT, 'R');
	if (dump)
		RS232_SendByte(COM_PORT, 'R');
	else
		RS232_SendByte(COM_PORT, 'W');
	waitRDY();

	printf("\n- Flasher ready\n");

	uint8_t id, manid;
	RS232_PollComport(COM_PORT, &manid, 1);
	printf("\nChip information:\n");
	printf(" Manufacturer ID: 0x%X\n Detected as: %s\n", manid, getManufacturer(manid));

	RS232_PollComport(COM_PORT, &id, 1);
	printf(" Device ID: 0x%X (", id);

	// Determine flash size.
	uint32_t capacity = 524288;
	switch (id) {
		case 0xB5:
			printf("SST39SF010A)\n");
			capacity = 131072;
			break;
		case 0xB6:
			printf("SST39SF020A)\n");
			capacity = 262144;
			break;
		case 0xB7:
			printf("SST39SF040)\n");
			// The variable capacity is already set to 524288.
			break;
		default:
			printf("ERROR: Cannot determine chip capacity, defaulting to 524288\n");
			break;
	}

	// The variable fp can refer either to the file that we are reading to flash or the file that will contain the flash memory's contents.
	FILE* fp;
	// A temporary buffer for data.
	uint8_t* dat = NULL;

	// Flashing mode.
	if (!dump) {
		// Open file for reading.
		fp = fopen(argv[2], "rb");

		// Validity check.
		if (!fp) {
			printf("ERROR: File cannot be opened\n");
			return 1;
		}

		// Get file size.
		fseek(fp, 0L, SEEK_END);
		size_t size = ftell(fp);

		// Check for size mismatch.
		if (size > capacity) {
			printf("ERROR: File too large (%lu - %u))\n", size, capacity);
			fclose(fp);
			return 1;
		}
		// Return to beginning of file.
		rewind(fp);

		// Allocate memory for binary data.
		dat = (uint8_t*)calloc(1, capacity);

		// Check for allocation errors.
		if (dat == 0) {
			printf("ERROR: Cannot allocate memory\n");
			fclose(fp);
			return 1;
		}

		// Read file data into array.
		fread(dat, 1, size, fp);

		// Close file.
		fclose(fp);

		// Flash erasing procedure.
		printf("\n- Erasing chip\n");
		RS232_PollComport(COM_PORT, &id, 1);
		if (id != 'D') {
			printf("\nAn error has occurred, exiting...\n");
			free(dat);
			return 1;
		}
		putchar('\n');
		RS232_PollComport(COM_PORT, &id, 1);
		if (id == 'S')
			printf("- Erasing complete\n");
		else {
			printf("ERROR: Erasing chip code %c failed\n", id);
			free(dat);
			return 1;
		}
		printf("\n- Begin flashing %s\n", argv[2]);

		// Dumping mode.
	} else {
		// Open file for writing.
		fp = fopen(argv[2], "wb");
		printf("\n- Begin dumping to %s\n", argv[2]);
	}

	// Flashing procedure.
	putchar('\n');
	uint32_t x;
	for (x = 0; x < capacity; ++x) {
		uint8_t data;
		if (dump) {
			RS232_PollComport(COM_PORT, &data, 1);
			fputc(data, fp);
		} else {
			programByte(dat[x]);
			RS232_PollComport(COM_PORT, &data, 1);
			if (data != dat[x])
				printf("Byte %d at address %d should be %d\n\n", data, x, dat[x]);
		}
		//if ((x & 255) == 0)
			printf("Progress : %% %f\r", (float)x / (float)capacity * 100.0);
	}

	printf("-------- COMPLETED --------\n\n");

	// Close serial connection.
	RS232_CloseComport(COM_PORT);

	// Close file.
	if (dump) fclose(fp);
	else free(dat);

	// Successful exit.
	return 0;
}
