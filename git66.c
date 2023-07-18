/* read a Garmin FIT file
 *
 * Tom Trebisky  7-17-2023
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

/* For htons and htonl
#include <arpa/inet.h>
*/

char *path = "/home/tom/c.fit";

/*

00000000 0e10 6d08 44bf 0000 2e46 4954 dd09 4000     m D   .FIT  @
00000010 0000 0007 0304 8c04 0486 0704 8601 0284
00000020 0202 8405 0284 0001 0000 5426 50c8 4185             T&P A
00000030 123f ffff ffff 0100 d40c ffff 0441 0000    ?           A
00000040 3100 0302 1407 0002 8401 0102 0100 0000   1
00000050 0000 0000 0000 0000 0000 0000 0000 0000

ls -l /home/tom/c.fit
-rw-r--r-- 1 tom tom 48980 Jul 14 12:55 /home/tom/c.fit

File size: 48978 bytes, protocol ver: 1.00, profile ver: 21.57
File header CRC: 0x09DD

len = 14
ver = 16
ver = 2157
f_len = 48964
sig = .FIT

*/

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

/* We get a 16 byte thing without the packed attribute */
/* We want 14 bytes */

struct __attribute__((__packed__)) fit_header {
	unsigned char len;
	unsigned char prot_ver;
	u16 prof_ver;
	u32 f_len;
	char sig[4];
	u16 crc;
};

/* --------------------------------------------------------------------- */
/* --------------------------------------------------------------------- */

struct global {
	int id;
	char *name;
};

/* This is incomplete.  I have only the global ID entries
 * that I see in the FIT file from my Garmin 66i.
 * So, "it works for me".
 */
struct global ginfo[] = {
    { 0, "file ID" },
    { 49, "file creator" },
    { 23, "device info" },
    { 20, "record" },
    { 21, "event" },
    { 19, "lap" },
    { 18, "session" },
    { 34, "activity" },
    { 0, NULL }
};

void
oops ( char *msg )
{
	fprintf ( stderr, "%s\n", msg );
	exit ( 1 );
}

/* Bits in the header byte */
#define H_COMP		0x80	/* compressed message */
#define H_DEF		0x40	/* definition message */
#define H_HASDEV	0x20	/* has developer fields */
#define H_XXX		0x10	/* --- */
#define H_ID		0x0f	/* ID mask */

struct __attribute__((__packed__)) field {
	u8 id;
	u8 size;
	u8 type;
};

struct definition {
	int size;
	int nf;
	struct field field[100];	/* XXX */
} def;

struct global *
global_lookup ( int gid )
{
	struct global *gp;

	gp = ginfo;

	while ( gp->name ) {
	    if ( gp->id == gid )
		return gp;
	    gp++;
	}
	return NULL;
}

/* ---------------------------------------------------------------------- */
/* IO routines.
 * rather than pass "fd" all around, we call these.
 * also implement a 1 byte peek.
 */

int fit_fd;

int
peek1 ( void )
{
	off_t pos = lseek ( fit_fd, (off_t) 0, SEEK_CUR );
	int n;
	u8 cbuf;

	n = read ( fit_fd, &cbuf, 1 );
	(void) lseek ( fit_fd, pos, SEEK_SET );

	return cbuf;
}

int
read1 ( void )
{
	int n;
	u8 cbuf;

	n = read ( fit_fd, &cbuf, 1 );

	return cbuf;
}

void
readn ( u8 *buf, int nbuf )
{
	int n;

	n = read ( fit_fd, buf, nbuf );
}

/* ---------------------------------------------------------------------- */

static u16
fit_crc16 ( u8 data, u16 crc)
{
  static const u16 crc_table[] = {
    0x0000, 0xcc01, 0xd801, 0x1400, 0xf001, 0x3c00, 0x2800, 0xe401,
    0xa001, 0x6c00, 0x7800, 0xb401, 0x5000, 0x9c01, 0x8801, 0x4400
  };

  crc = (crc >> 4) ^ crc_table[crc & 0xf] ^ crc_table[data & 0xf];
  crc = (crc >> 4) ^ crc_table[crc & 0xf] ^ crc_table[(data >> 4) & 0xf];
  return crc;
}

void
check_crc ( void )
{
	u8 data;
	u16 crc = 0;
	int n;

	for ( ;; ) {
	    n = read ( fit_fd, &data, 1 );
	    if ( n == 0 )
		break;
	    crc = fit_crc16 ( data, crc );
	}
	printf ( "CRC for entire file: %04x\n", crc );
	lseek ( fit_fd, 0, SEEK_SET );
	if ( crc )
	    oops ( "Bad file CRC" );
}

void
check_header_crc ( u8 *h, int size )
{
	u16 crc = 0;
	int i;

	for ( i=0; i<size; i++ )
	    crc = fit_crc16 ( h[i], crc );
	printf ( "CRC for header: %04x\n", crc );
	if ( crc )
	    oops ( "Bad header CRC" );
}


/* ---------------------------------------------------------------------- */

struct __attribute__((__packed__)) def_hdr {
	u8	header;
	u8	reserved;
	u8	endian;
	u16	g_id;
	u8	nf;
};


int
definition_record ( void )
{
	int id;
	// int n;
	int i;
	struct def_hdr dhdr;

	int size;
	int nf;
	u8 nd;		/* It is critical that this be u8 */
	int ndev = 0;

	struct field ff;
	struct global *gp;

	// printf ( "Sizeof def header = %d\n", sizeof(struct def_hdr) );
	// n = read ( fd, (char *) &dhdr, sizeof(struct def_hdr) );
	readn ( (u8 *) &dhdr, sizeof(struct def_hdr) );

	/* This header ID is just a sequential count 0, 1, ... */
	id = dhdr.header & H_ID;
	printf ( "Definition record, header = 0x%02x, header id = %d\n", dhdr.header, id );


	gp = global_lookup ( dhdr.g_id );
	if ( ! gp ) {
	    printf ( "gid = %d\n", dhdr.g_id );
	    oops ( "Alligator attack" );
	}

	printf ( "Definition record, global ID = %d -- %s\n", dhdr.g_id, gp->name );

	// printf ( "Sizeof field: %d\n", sizeof(struct field) );
	nf = dhdr.nf;

	size = 0;
	for ( i=0; i<nf; i++ ) {
	    // n = read ( fd, &ff, sizeof(struct field) );
	    readn ( (u8 *) &ff, sizeof(struct field) );
	    printf ( "-- Field: %d, id, size, type = %d %d %d(0x%02x)\n", i, ff.id, ff.size, ff.type, ff.type );
	    size += ff.size;
	    def.field[i] = ff;
	}
	def.nf = nf;

	/* We do see these!
	 * We just read and discard.
	 * Note that the header bit may be set, but the count be zero.
	 */
	if ( dhdr.header & H_HASDEV ) {
	    // oops ( "Developer fields" );
	    // n = read ( fd, &nd, 1 );
	    nd = read1 ();
	    ndev += 1;
	    printf ( "Developer fields = %d\n", nd );

	    for ( i=0; i<nd; i++ ) {
		readn ( (u8 *) &ff, sizeof(struct field) );
		printf ( "-- Dev Field: %d, id, size, type = %d %d %d\n", i, ff.id, ff.size, ff.type );
	    }
	    ndev += nd*sizeof(struct field);
	}

	printf ( " expected record size will be %d bytes\n", size );
	def.size = size;

	return sizeof(struct def_hdr) + nf*sizeof(struct field) + ndev;
}

/* Here is what the data "record" records from the 66i look like:
 * My sample file has 1175 of these

 Definition record, global ID = 20 -- record
-- Field: 0, id, size, type = 253 4 134(0x86)  timestamp
-- Field: 1, id, size, type = 0 4 133(0x85)    lat
-- Field: 2, id, size, type = 1 4 133(0x85)    long
-- Field: 3, id, size, type = 5 4 134(0x86)    dist
-- Field: 4, id, size, type = 14 4 134(0x86)   ?
-- Field: 5, id, size, type = 15 4 134(0x86)   ?
-- Field: 6, id, size, type = 73 4 134(0x86)    enh speed
-- Field: 7, id, size, type = 78 4 134(0x86)    enh altitude
-- Field: 8, id, size, type = 2 2 132(0x84)    altitude
-- Field: 9, id, size, type = 6 2 132(0x84)    speed
-- Field: 10, id, size, type = 3 1 2(0x02)    heart rate
-- Field: 11, id, size, type = 4 1 2(0x02)    cadence
-- Field: 12, id, size, type = 13 1 1(0x01)    temperature
-- Field: 13, id, size, type = 53 1 2(0x02)    fractional cadence
 expected record size will be 40 bytes

 * I also have 2 "event" records like so:
 * These come immediately after the above records.

 Definition record, global ID = 21 -- event
-- Field: 0, id, size, type = 253 4 134(0x86)	timestamp
-- Field: 1, id, size, type = 3 4 134(0x86)	data
-- Field: 2, id, size, type = 0 1 0(0x00)	event
-- Field: 3, id, size, type = 1 1 0(0x00)	event type
-- Field: 4, id, size, type = 4 1 2(0x02)	event group
 expected record size will be 11 bytes

 */

int
data_record ( struct definition *dp )
{
	int header;
	int id;
	// int n;
	char buf[256];		/* XXX - 128 would do */

	header = read1 ();
	id = header & H_ID;
	printf ( "Data record, header id = %d (%d bytes)\n", id, dp->size  );

	readn ( buf, dp->size );

	return 1 + dp->size;
}

int
record ( void )
{
	// u8 header;
	// int n;
	int header;
	int nn;

	// n = read ( fd, &header, 1 );
	header = peek1 ();

	printf ( "\n" );
	printf ( "Record header: 0x%02x\n", header );

	if ( header & H_COMP ) {
	    printf ( "Compressed record\n" );
	    oops ( "Not ready for compressed records" );
	} else if ( header & H_DEF ) {
	    printf ( "Definition record\n" );
	    nn = definition_record ();
	} else {
	    printf ( "Data record\n" );
	    nn = data_record ( &def );
	}

	// return 1 + nn;
	return nn;
}

/* Read file header */
int
header ( void )
{
	struct fit_header hdr;
	int n;

	printf ( "header size expected to be: %d\n", sizeof(struct fit_header) );

	readn ( (u8 *)&hdr, sizeof(struct fit_header) );

	if ( strncmp ( hdr.sig, ".FIT", 4 ) == 0 )
	    printf ( "Signature is OK\n" );
	else {
	    printf ( "Not a FIT file\n" );
	    exit ( 2 );
	}

	check_header_crc ( (char *) &hdr, hdr.len );

	printf ( "len = %d\n", hdr.len );
	printf ( "ver = %d\n", hdr.prot_ver );
	printf ( "ver = %d\n", hdr.prof_ver );
	printf ( "f_len = %ld\n", hdr.f_len );		/* data length */
	printf ( "sig = %.4s\n", hdr.sig );
	printf ( "crc = %04x\n", hdr.crc );		/* CRC for header */

	/* The file size is 48980 bytes.
	 * F-len in the header is 48964
	 * A difference of 16 bytes.
	 * The header is 14 (counting the CRC)
	 * and the file has a 2 byte trailing CRC.
	 */
	return hdr.f_len;
}

int
main ( int argc, char **argv )
{
	int fd;
	int nio;
	int nrec;

	fd = open ( path, O_RDONLY );
	if ( fd < 0 )
	    oops ( "open" );
	fit_fd = fd;

	check_crc ();

	nio = header ();

	while ( nio > 0 ) {
	    printf ( "%d bytes left in file\n", nio );
	    nrec = record ();
	    nio -= nrec;
	}

	if ( nio != 0 ) {
	    printf ( "%d bytes left in file\n", nio );
	    oops ( "Buffalo stampede" );
	}

	printf ( "All done\n" );
	return 0;
}

/* THE END */
