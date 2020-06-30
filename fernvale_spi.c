/*
 * This file is part of the flashrom project.
 *
 * Copyright (C) 2009, 2010 Carl-Daniel Hailfinger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "flash.h"
#include "programmer.h"
#include "spi.h"

#define DEFAULT_DEV "/dev/fernvale"

static int fernvale_spi_send_command(struct flashctx *flash,
				    unsigned int writecnt, unsigned int readcnt,
				    const unsigned char *writearr,
				    unsigned char *readarr);

static const struct spi_master spi_master_fernvale = {
	.type		= SPI_CONTROLLER_FERNVALE,
	.max_data_read	= 128,
	.max_data_write	= 128,
	.command	= fernvale_spi_send_command,
	.multicommand	= default_spi_send_multicommand,
	.read		= default_spi_read,
	.write_256	= default_spi_write_256,
	.write_aai	= default_spi_write_aai,
};

static int fernvale_spi_shutdown(void *data)
{
	int ret = 0, ret2 = 0;
	const unsigned char cmd[] = { 0, 0 };

	ret = serialport_write(cmd, sizeof(cmd));
	ret2 = serialport_shutdown(NULL);
	if (ret2 && !ret)
		ret = ret2;

	return ret;
}

static int fernvale_serialport_setup(char *dev)
{
	/* 115200bps, 8 databits, no parity, 1 stopbit */
	sp_fd = sp_openserport(dev, 921600);
	if (sp_fd == SER_INV_FD)
		return 1;
	return 0;
}

int fernvale_spi_init(void)
{
	char *dev;
	int ret = 0;

	dev = extract_programmer_param("dev");
	if (dev && !strlen(dev)) {
		free(dev);
		dev = NULL;
	}
	if (!dev)
		dev = DEFAULT_DEV;

	ret = fernvale_serialport_setup(dev);
	free(dev);
	if (ret) {
		return ret;
	}

	const unsigned char cmd[] = "spi flashrom\n";
	unsigned char readback;
	int ready_tries = 0;
	serialport_write(cmd, 12);

	/* Look for "Ready" signal */
	do {
		serialport_read(&readback, sizeof(readback));
		ready_tries++;
	} while (readback != 0x05);
	msg_gdbg("Found 'ready' signal after %d bytes\n", ready_tries);

	register_spi_master(&spi_master_fernvale);
	register_shutdown(fernvale_spi_shutdown, NULL);

	return 0;
}

static int write_full(const void *bfr, int size)
{
	int ret;
	int left = size;

	while (left > 0) {
		ret = serialport_write(bfr, left);
		if (ret == -1) {
			if (errno == EAGAIN)
				continue;
			return ret;
		}

		/* FD closed */
		if (!ret)
			return -1;

		left -= ret;
		bfr  += ret;
	}
	return size;
}

static int read_full(void *bfr, int size)
{
	int ret;
	int left = size;

	msg_gdbg(" Reading %d bytes:", size);
	while (left > 0) {
		ret = serialport_read(bfr, 1);
		if (ret == -1) {
			if (errno == EAGAIN)
				continue;
			return ret;
		}
		msg_gdbg(" 0x%02x:", *((uint8_t *)bfr));
		
		/* FD closed */
		if (!ret)
			return -1;

		left -= ret;
		bfr  += ret;
	}
	return size;
}

static int fernvale_spi_send_command(struct flashctx *flash,
				    unsigned int writecnt, unsigned int readcnt,
				    const unsigned char *writearr,
				    unsigned char *readarr)
{
	uint8_t out_bytes = writecnt;
	uint8_t in_bytes = readcnt;
	int ret;
	int i;
#if 1
	ret = write_full(&out_bytes, sizeof(out_bytes));
	if (ret != sizeof(out_bytes))
		msg_perr("0: Wanted to write %d bytes, but got %d\n",
				(int)sizeof(out_bytes), ret);

	ret = write_full(&in_bytes, sizeof(in_bytes));
	if (ret != sizeof(in_bytes))
		msg_perr("1: Wanted to write %d bytes, but got %d\n",
				(int)sizeof(in_bytes), ret);

	ret = write_full(writearr, out_bytes);
	if (ret != out_bytes)
		msg_perr("0: Wanted to write %d bytes, but got %d\n",
				out_bytes, ret);

	msg_gdbg("  Wrote %d bytes:", out_bytes);
	for (i = 0; i < out_bytes; i++)
		msg_gdbg(" %02x", writearr[i]);
	msg_gdbg("  ");
#else
	uint8_t bfr[writecnt + 2];

	bfr[0] = out_bytes;
	bfr[1] = in_bytes;
	memcpy(&bfr[2], writearr, out_bytes);

	ret = write_full(bfr, sizeof(bfr));
	if (ret != sizeof(bfr))
		msg_perr("0: Wanted to write %d bytes, but got %d\n",
				sizeof(bfr), ret);

	msg_gdbg("  Wrote %d bytes:", sizeof(bfr));
	for (i = 0; i < sizeof(bfr); i++)
		msg_gdbg(" %02x", bfr[i]);
	msg_gdbg("  ");
#endif

	ret = read_full(readarr, in_bytes);
	if (ret != in_bytes)
		msg_perr("3: Wanted to read %d bytes, but got %d\n",
				in_bytes, ret);

	msg_gdbg("  Read %d bytes:", in_bytes);
	for (i = 0; i < in_bytes; i++)
		msg_gdbg(" %02x", readarr[i]);
	msg_gdbg("  ");

	msg_gdbg("\n");

	return 0;
}
