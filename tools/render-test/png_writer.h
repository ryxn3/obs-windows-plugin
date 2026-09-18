#pragma once
/* minimal PNG writer (stored deflate blocks), RGBA8 - shared by the test tools */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static uint32_t crc_table[256];
static void crc_init(void)
{
	for (uint32_t n = 0; n < 256; n++) {
		uint32_t c = n;
		for (int k = 0; k < 8; k++)
			c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		crc_table[n] = c;
	}
}
static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len)
{
	uint32_t c = crc;
	for (size_t i = 0; i < len; i++)
		c = crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
	return c;
}
static void put32(FILE *f, uint32_t v)
{
	uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
	fwrite(b, 1, 4, f);
}
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
	put32(f, len);
	fwrite(type, 1, 4, f);
	if (len)
		fwrite(data, 1, len, f);
	uint32_t crc = crc_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
	if (len)
		crc = crc_update(crc, data, len);
	put32(f, crc ^ 0xFFFFFFFFu);
}

/* minimal PNG writer (stored deflate blocks), RGBA8 */
static bool write_png(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
	crc_init();
	size_t rowb = (size_t)w * 4 + 1;
	size_t rawlen = rowb * h;
	uint8_t *raw = malloc(rawlen);
	for (uint32_t y = 0; y < h; y++) {
		raw[y * rowb] = 0;
		memcpy(raw + y * rowb + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
	}
	size_t nblocks = (rawlen + 65534) / 65535;
	size_t zlen = 2 + rawlen + nblocks * 5 + 4;
	uint8_t *z = malloc(zlen);
	size_t o = 0;
	z[o++] = 0x78;
	z[o++] = 0x01;
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < rawlen; i++) {
		a = (a + raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	for (size_t off = 0; off < rawlen;) {
		size_t n = rawlen - off > 65535 ? 65535 : rawlen - off;
		z[o++] = (off + n == rawlen) ? 1 : 0;
		z[o++] = (uint8_t)(n & 0xFF);
		z[o++] = (uint8_t)(n >> 8);
		z[o++] = (uint8_t)(~n & 0xFF);
		z[o++] = (uint8_t)((~n >> 8) & 0xFF);
		memcpy(z + o, raw + off, n);
		o += n;
		off += n;
	}
	uint32_t ad = (b << 16) | a;
	z[o++] = (uint8_t)(ad >> 24);
	z[o++] = (uint8_t)(ad >> 16);
	z[o++] = (uint8_t)(ad >> 8);
	z[o++] = (uint8_t)ad;

	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	fwrite(sig, 1, 8, f);
	uint8_t ihdr[13] = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
			    (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
			    8, 6, 0, 0, 0};
	chunk(f, "IHDR", ihdr, 13);
	chunk(f, "IDAT", z, (uint32_t)o);
	chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(raw);
	free(z);
	return true;
}

