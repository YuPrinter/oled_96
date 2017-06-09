// OLED SSD1306 using the I2C interface
// Written by Larry Bank (bitbank@pobox.com)
// Project started 1/15/2017
//
// The I2C writes (through a file handle) can be single or multiple bytes.
// The write mode stays in effect throughout each call to write()
// To write commands to the OLED controller, start a byte sequence with 0x00,
// to write data, start a byte sequence with 0x40,
// The OLED controller is set to "page mode". This divides the display
// into 8 128x8 "pages" or strips. Each data write advances the output
// automatically to the next address. The bytes are arranged such that the LSB
// is the topmost pixel and the MSB is the bottom.
// The font data comes from another source and must be rotated 90 degrees
// (at init time) to match the orientation of the bits on the display memory.
// A copy of the display memory is maintained by this code so that single pixel
// writes can occur without having to read from the display controller.

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

extern unsigned char ucFont[];
static int iScreenOffset; // current write offset of screen data
static unsigned char ucScreen[1024]; // local copy of the image buffer
static int file_i2c = 0;

static void oledWriteCommand(unsigned char);
static void RotateFont90(void);

// Opens a file system handle to the I2C device
// Initializes the OLED controller into "page mode"
// Prepares the font data for the orientation of the display
int oledInit(int iAddr)
{
const unsigned char initbuf[]={0x00,0xae,0xa8,0x3f,0xd3,0x00,0x40,0xa0,0xa1,0xc0,0xc8,
			0xda,0x12,0x81,0xff,0xa4,0xa6,0xd5,0x80,0x8d,0x14,
			0xaf,0x20,0x02};
char *filename = "/dev/i2c-1";

	if ((file_i2c = open(filename, O_RDWR)) < 0)
	{
		fprintf(stderr, "Failed to open the i2c bus\n");
		file_i2c = 0;
		return -1;
	}

	if (ioctl(file_i2c, I2C_SLAVE, iAddr) < 0)
	{
		fprintf(stderr, "Failed to acquire bus access or talk to slave\n");
		file_i2c = 0;
		return -1;
	}

	write(file_i2c, initbuf, sizeof(initbuf));
	RotateFont90(); // fix font orientation for OLED
	return 0;

} /* oledInit() */

// Sends a command to turn off the OLED display
// Closes the I2C file handle
void oledShutdown()
{
	if (file_i2c != 0)
	{
		oledWriteCommand(0xaE); // turn off OLED
		close(file_i2c);
		file_i2c = 0;
	}
}

// Send a single byte command to the OLED controller
static void oledWriteCommand(unsigned char c)
{
unsigned char buf[2];
	buf[0] = 0x00; // command introducer
	buf[1] = c;
	write(file_i2c, buf, 2);
} /* oledWriteCommand() */

static void oledWriteCommand2(unsigned char c, unsigned char d)
{
unsigned char buf[3];
	buf[0] = 0x00;
	buf[1] = c;
	buf[2] = d;
	write(file_i2c, buf, 3);
} /* oledWriteCommand2() */

int oledSetContrast(unsigned char ucContrast)
{
        if (file_i2c == 0)
                return -1;

	oledWriteCommand2(0x81, ucContrast);
	return 0;
} /* oledSetContrast() */

//static void oledWriteData(unsigned char c)
//{
//unsigned char buf[2];
//        buf[0] = 0x40; // data introducer
//        buf[1] = c;
//        write(file_i2c, buf, 2);
//} /* oledWriteData() */

// Send commands to position the "cursor" to the given
// row and column
static void oledSetPosition(int x, int y)
{
	oledWriteCommand(0xb0 | y); // go to page Y
	oledWriteCommand(0x00 | (x & 0xf)); // // lower col addr
	oledWriteCommand(0x10 | ((x >> 4) & 0xf)); // upper col addr
	iScreenOffset = (y*128)+x;
}

//static void oledWrite(unsigned char c)
//{
//	write(file_i2c, &c, 1);
//} /* oledWrite() */

// Write a block of pixel data to the OLED
// Length can be anything from 1 to 1024 (whole display)
static void oledWriteDataBlock(unsigned char *ucBuf, int iLen)
{
unsigned char ucTemp[129];

	ucTemp[0] = 0x40; // data command
	memcpy(&ucTemp[1], ucBuf, iLen);
	write(file_i2c, ucTemp, iLen+1);
	// Keep a copy in local buffer
	memcpy(&ucScreen[iScreenOffset], ucBuf, iLen);
	iScreenOffset += iLen;
}

// Set (or clear) an individual pixel
// The local copy of the frame buffer is used to avoid
// reading data from the display controller
int oledSetPixel(int x, int y, unsigned char ucColor)
{
int i;
unsigned char uc, ucOld;

	if (file_i2c == 0)
		return -1;

	i = ((y >> 3) * 128) + x;
	if (i < 0 || i > 1023) // off the screen
		return -1;
	uc = ucOld = ucScreen[i];
	uc &= ~(0x1 << (y & 7));
	if (ucColor)
	{
		uc |= (0x1 << (y & 7));
	}
	if (uc != ucOld) // pixel changed
	{
		oledSetPosition(x, y>>3);
		oledWriteDataBlock(&uc, 1);
	}
	return 0;
} /* oledSetPixel() */

// Draw a string of small (8x8) or large (16x24) characters
// At the given col+row
int oledWriteString(int x, int y, char *szMsg, int bLarge)
{
int i, iLen;
unsigned char *s;

	if (file_i2c == 0) return -1; // not initialized

	iLen = strlen(szMsg);
	if (bLarge) // draw 16x32 font
	{
		if (iLen+x > 8) iLen = 8-x;
		if (iLen < 0) return -1;
		x *= 16;
		for (i=0; i<iLen; i++)
		{
			s = &ucFont[9728 + (unsigned char)szMsg[i]*64];
			oledSetPosition(x+(i*16), y);
			oledWriteDataBlock(s, 16);
			oledSetPosition(x+(i*16), y+1);
			oledWriteDataBlock(s+16, 16);	
			oledSetPosition(x+(i*16), y+2);
			oledWriteDataBlock(s+32, 16);	
//			oledSetPosition(x+(i*16), y+3);
//			oledWriteDataBlock(s+48, 16);	
		}
	}
	else // draw 8x8 font
	{
		oledSetPosition(x, y);
		if (iLen + x > 16) iLen = 16 - x; // can't display it
		if (iLen < 0)return -1;

		for (i=0; i<iLen; i++)
		{
			s = &ucFont[(unsigned char)szMsg[i] * 8];
			oledWriteDataBlock(s, 8); // write character pattern
		}	
	}
	return 0;
} /* oledWriteString() */

// Fill the frame buffer with a byte pattern
// e.g. all off (0x00) or all on (0xff)
int oledFill(unsigned char ucData)
{
int y;
unsigned char temp[128];

	if (file_i2c == 0) return -1; // not initialized

	memset(temp, ucData, 128);
	for (y=0; y<8; y++)
	{
		oledSetPosition(0,y); // set to (0,Y)
		oledWriteDataBlock(temp, 128); // fill with data byte
	} // for y
	return 0;
} /* oledFill() */

// Fix the orientation of the font image data
static void RotateFont90(void)
{
unsigned char ucTemp[64];
int i, j, x, y;
unsigned char c, c2, ucMask, *s, *d;

	// Rotate the 8x8 font
	for (i=0; i<256; i++) // fix 8x8 font by rotating it 90 deg clockwise
	{
		s = &ucFont[i*8];
		ucMask = 0x1;
		for (y=0; y<8; y++)
		{
			c = 0;
			for (x=0; x<8; x++)
			{
				c >>= 1;
				if (s[x] & ucMask) c |= 0x80;
			}
			ucMask <<= 1;
			ucTemp[7-y] = c;
		}
		memcpy(s, ucTemp, 8);
	}
	// Rotate the 16x32 font
	for (i=0; i<128; i++) // only 128 characters
	{
		for (j=0; j<4; j++)
		{
			s = &ucFont[9728 + 12 + (i*64) + (j*16)];
			d = &ucTemp[j*16];
			ucMask = 0x1;
			for (y=0; y<8; y++)
			{
				c = c2 = 0;
				for (x=0; x<8; x++)
				{
					c >>= 1;
					c2 >>= 1;
					if (s[(x*2)] & ucMask) c |= 0x80;
					if (s[(x*2)+1] & ucMask) c2 |= 0x80;
				}
				ucMask <<= 1;
				d[7-y] = c;
				d[15-y] = c2;
			} // for y
		} // for j
		memcpy(&ucFont[9728 + (i*64)], ucTemp, 64);
	} // for i
} /* RotateFont90() */
