/* read a Garmin FIT file
 *  from my Garmin 66i
 *
 * This is NOT a general FIT file reader.
 * There are lots of other possible record types and
 * field types.  I have only implemented those things
 * that I have seen in the files from my Garmin 66i
 *
 * So this solves problems for me.  You may find it useful
 * if you want to learn about the FIT file format.
 *
 * Tom Trebisky  7-17-2023
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <time.h>
#include <math.h>

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

/* Some global variables.
 */
int fit_fd;
int record_count;
int dump_level = 0;

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

off_t
ltell ( int fd )
{
	return lseek ( fd, (off_t) 0, SEEK_CUR );
}

off_t
fit_tell ( void )
{
	return ltell ( fit_fd );
}

void
fit_seek ( off_t pos )
{
	(void) lseek ( fit_fd, pos, SEEK_SET );
}

int
peek1 ( void )
{
	off_t pos;
	int n;
	u8 cbuf;

	pos = fit_tell ();
	n = read ( fit_fd, &cbuf, 1 );
	fit_seek ( pos );

	return cbuf;
}

int
read1 ( void )
{
	u8 cbuf;

	(void) read ( fit_fd, &cbuf, 1 );

	return cbuf;
}

int
read2 ( void )
{
	u16 sbuf;

	(void) read ( fit_fd, &sbuf, 2 );

	return sbuf;
}

int
read4 ( void )
{
	u32 ibuf;

	(void) read ( fit_fd, &ibuf, 4 );

	return ibuf;
}

void
readn ( u8 *buf, int nbuf )
{
	int n;

	n = read ( fit_fd, buf, nbuf );
}

/* ---------------------------------------------------------------------- */
/* General geometry and math --
 *
 * Converting lat/long to miles (or feet, or meters)
 * The earth is an ellipsoid.
 * WGS84 is the model we use for it.
 *
 * At 38 degrees north (according to the USGS) :
 *  1 degree of latitude is about 364,000 feet (69 miles)
 *  1 degree of longitude is 288,200 feet (54.6 miles)
 *
 * In what follows, the "a" value (semi-major axis) is
 *    the radius of the earth at the equator.
 * At the pole, the distance is 6356752.3 meters
 *    (a difference of 21384.7 meters (21.4 km))
 *
 * The following code gives:
    38.0 degrees North latitude
	long (fpd) = 288164.26
	lat  (fpd) = 364161.71

 */

void
wgs84 ( double dlat, double *long_fpd, double *lat_fpd )
{
	double a = 6378137.0;		// semi-major axis, meters
	// double f;
	// double flat;
	double e, ee, es, ess;
	double lat;
	double m, r;
	double mf, rf;
	double dd, div1, div2;
	double pi = 3.1415929;
	double d2r = pi / 180.0;
	double m2f = 3.28084;		// meters to feet

	// dlat = 38.0;
	// dlat = 0.0;

	lat = dlat * d2r;
	// flat = 298.257223563;	// flattening factor
	// f = 1.0 / flat;

	// e = sqrt(2f-f**2);
	e = 0.081819191;		// eccentricity
	ee = e * e;
	es = e * sin(lat);
	ess = es * es;

	dd = 1.0 - ess;
	div2 = sqrt ( dd );
	div1 = dd * div2;

	// div1 = pow ( (1.0 - ee *sin(lat)**2), 1.5);
	// div2 = pow ( (1.0 - ee *sin(lat)**2), 0.5);

	m = a*(1.0 - ee) / div1;	// meridian radius of curvature
	r = a*cos(lat) / div2;		// curvature of parallels

	/* To get a one degree increment.
	 * Apparently the above values are the for a radian of curvature.
	 */
	m *= d2r;
	r *= d2r;

	/* Convert to feet */
	*lat_fpd = m * m2f;
	*long_fpd = r * m2f;
}

void
wgs_test ( void )
{
	double long_fpd;
	double lat_fpd;
	double dlat = 38.0;

	wgs84 ( dlat, &long_fpd, &lat_fpd );

	printf ( "%.1f degrees North latitude\n", dlat );
	printf ( "long (fpd) = %.2f\n", long_fpd );
	printf ( "lat  (fpd) = %.2f\n", lat_fpd );
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
	if ( dump_level > 1 )
	    printf ( "CRC for entire file: %04x\n", crc );

	lseek ( fit_fd, 0, SEEK_SET );

	if ( crc )
	    oops ( "Bad file CRC" );
}

int
calc_crc ( u8 *buf, int n )
{
	int i;
	u16 crc = 0;

	for ( i=0; i<n; i++ )
	    crc = fit_crc16 ( buf[i], crc );
	return crc;
}

void
check_header_crc ( u8 *h, int size )
{
	u16 crc = 0;
	int i;

	for ( i=0; i<size; i++ )
	    crc = fit_crc16 ( h[i], crc );
	if ( dump_level > 1 )
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

	if ( dump_level > 1 ) {
	    printf ( "\n" );
	    printf ( "Definition record, header = 0x%02x, header id = %d\n", dhdr.header, id );
	}

	gp = global_lookup ( dhdr.g_id );
	if ( ! gp ) {
	    printf ( "gid = %d\n", dhdr.g_id );
	    oops ( "Alligator attack" );
	}

	if ( dump_level > 1 )
	    printf ( "Definition record, global ID = %d -- %s\n", dhdr.g_id, gp->name );

	// printf ( "Sizeof field: %d\n", sizeof(struct field) );
	nf = dhdr.nf;

	size = 0;
	for ( i=0; i<nf; i++ ) {
	    // n = read ( fd, &ff, sizeof(struct field) );
	    readn ( (u8 *) &ff, sizeof(struct field) );
	    if ( dump_level > 1 )
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
	    if ( dump_level > 1 )
		printf ( "Developer fields = %d\n", nd );

	    for ( i=0; i<nd; i++ ) {
		readn ( (u8 *) &ff, sizeof(struct field) );
		if ( dump_level > 1 )
		    printf ( "-- Dev Field: %d, id, size, type = %d %d %d\n", i, ff.id, ff.size, ff.type );
	    }
	    ndev += nd*sizeof(struct field);
	}

	if ( dump_level > 1 )
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

struct data {
	u32	time;
	double lon;
	double lat;
	double alt;
	double temp;
	double speed;
	double distance;
};

/* XXX - bogus limit on points */
struct data data[2000];
int ndata = 0;

/* Garmin uses an angular unit the call "semicircles".
 * The basic idea is that 2*pi radians uses all of the 32 bit resolution.
 * So pi radians is 0x80000000
 * angleInSemicircles = (angleInRadians * 0x80000000) / PI
 * degrees = semicircles * (180 / 2^31)
 */

double
cc2deg ( int cc_val )
{
	double rv;
	double div = 0x80000000;

	rv = cc_val;
	rv /= div;
	return rv * 180.0;
}

/* Offset in seconds from the unix epoch
 */
#define	FIT_OFFSET	631065600

/* This yields: Thu Jul 13 03:24:47 2023
 * (which is 5 words
 * strftime would allow any format,
 *  as well as not getting the newline
 */
char *
tstamp ( u32 gtime )
{
	time_t	tt;
	struct tm *tp;
	static char cbuf[64];

	// time_t is an 8 byte thing
	// printf ( "Sizeof time = %d\n", sizeof(time_t) );
	tt = gtime;
	tt += FIT_OFFSET;
	tp = localtime ( &tt );
	// printf ( "Time: %s\n", asctime ( tp ) );
	// return asctime ( tp );
	strcpy ( cbuf,  asctime ( tp ) );
	// printf ( "Cbuf = %s %d\n", cbuf, strlen(cbuf) );
	cbuf[strlen(cbuf)-1] = '\0';
	return cbuf;
}

/* ID values --
 *  2 (altitude) is always 0xffff - use 78 instead
 *  3 (heart rate) is always 0xff - I ain't got no sensor
 *  4 (cadence) is always 0xff
 *  6 (speed) is always 0xffff - use 73 instead
 *  14 is always 0xffffffff
 *  15 is always 0xffffffff
 *  53 (fract cadence) is always 0xff
 */

void
decode ( struct definition *dp )
{
	int i;
	struct field *fp;
	int val;
	int lon;
	int lat;
	u32 time;
	double alt;
	double temp, speed, dist;

	for ( i=0; i<dp->nf; i++ ) {
	    fp = &dp->field[i];
	    if ( fp->size == 1 )
		val = read1 ();
	    else if ( fp->size == 2 )
		val = read2 ();
	    else if ( fp->size == 4 )
		val = read4 ();
	    else
		oops ( "Turkey riot" );

	    // printf ( "%d %5d %d %02x = %d\n", i, fp->id, fp->size, fp->type, val );

#define TS_ID		253
#define LAT_ID		0
#define LON_ID		1
#define ALT_ID		78
#define TEMP_ID		13
#define SPEED_ID	73
#define DIST_ID		5

#define M2F	3.280839895

	    // if ( fp->id == 53 )
	    // 	printf ( "53 = %08x\n", val );

	    if ( fp->id == TS_ID )
		time = val;
	    if ( fp->id == LAT_ID )
		lat = val;
	    if ( fp->id == LON_ID )
		lon = val;
	    if ( fp->id == ALT_ID )
		alt = val;
	    if ( fp->id == TEMP_ID )
		temp = val * 1.8 + 32.0;
	    if ( fp->id == SPEED_ID )
		speed = val / 1000.0;
	    if ( fp->id == DIST_ID )
		dist = val / 100.0;
	}

	alt = alt/5.0 - 500.0;
	alt *= M2F;

	/* Convert from m/s to miles/hour */
	speed *= 2.23694;

	/* Convert meters to miles */
	dist *= M2F;
	dist /= 5280.0;

	// printf ( "lon = %08x, %d\n", lon, lon );
	// printf ( "lat = %08x, %d\n", lat, lat );

	// printf ( "   lon, lat, alt = %.5f %.5f %.2f\n", cc2deg(lon), cc2deg(lat), alt );

	data[ndata].time = time;
	data[ndata].lon = cc2deg(lon);
	data[ndata].lat = cc2deg(lat);
	data[ndata].alt = alt;
	data[ndata].temp = temp;
	data[ndata].speed = speed;
	data[ndata].distance = dist;
	ndata++;
}

int
data_record ( struct definition *dp, int do_decode )
{
	int header;
	int id;
	// int n;
	char buf[256];		/* XXX - 128 would do */

	header = read1 ();
	id = header & H_ID;

	/* Don't show all 1175 records */
	if ( dp->size == 40 ) {
	    record_count++;
	    if ( do_decode )
		decode ( dp );
	} else {
	    if ( dump_level > 1 )
		printf ( "Data record, header id = %d (%d bytes)\n", id, dp->size  );
	    readn ( buf, dp->size );
	}

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

	// printf ( "\n" );
	// printf ( "Record header: 0x%02x\n", header );

	if ( header & H_COMP ) {
	    printf ( "Compressed record\n" );
	    oops ( "Not ready for compressed records" );
	} else if ( header & H_DEF ) {
	    if ( dump_level > 1 && record_count )
		printf ( " %d data records (not shown)\n", record_count );
	    // printf ( "Definition record\n" );
	    nn = definition_record ();
	    record_count = 0;
	} else {
	    // printf ( "Data record\n" );
	    nn = data_record ( &def, 1 );
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

	// printf ( "header size expected to be: %d\n", sizeof(struct fit_header) );

	readn ( (u8 *)&hdr, sizeof(struct fit_header) );

	if ( strncmp ( hdr.sig, ".FIT", 4 ) == 0 ) {
	    if ( dump_level > 1 )
		printf ( "Signature is OK\n" );
	} else {
	    printf ( "Not a FIT file\n" );
	    exit ( 2 );
	}

	check_header_crc ( (char *) &hdr, hdr.len );

	if ( dump_level > 1 ) {
	    printf ( "len = %d\n", hdr.len );
	    printf ( "ver = %d\n", hdr.prot_ver );
	    printf ( "ver = %d\n", hdr.prof_ver );
	    printf ( "f_len = %ld\n", hdr.f_len );	/* data length */
	    printf ( "sig = %.4s\n", hdr.sig );
	    printf ( "crc = %04x\n", hdr.crc );		/* CRC for header */
	}

	/* The file size is 48980 bytes.
	 * F-len in the header is 48964
	 * A difference of 16 bytes.
	 * The header is 14 (counting the CRC)
	 * and the file has a 2 byte trailing CRC.
	 */
	return hdr.f_len;
}

void
open_fit ( void )
{
	int fd;

	fd = open ( path, O_RDONLY );
	if ( fd < 0 )
	    oops ( "Cannot open input FIT file" );
	fit_fd = fd;
}

void
read_file ( void )
{
	int nio;
	int nrec;

	open_fit ();
	check_crc ();

	nio = header ();

	while ( nio > 0 ) {
	    // printf ( "%d bytes left in file\n", nio );
	    nrec = record ();
	    nio -= nrec;
	}

	if ( nio != 0 ) {
	    printf ( "%d bytes left in file\n", nio );
	    oops ( "Buffalo stampede" );
	}

	close ( fit_fd );

	// printf ( "All done\n" );
	// printf ( "File read successfully: %d data points\n", ndata );
}

/* -------------------------------------------------------- */
/* Trim stuff */

/* My test file with 1175 record points is just
 * under 50k, and has over 7 hours of data.
 * The following should accomodate 6000 points and
 * well over 24 hours of data.
 */
#define TRIM_MAX	300000

u8 trim_buf[TRIM_MAX];
int ntrim = 0;

void
trim_append ( u8 *buf, int n )
{
	if ( ntrim + n > TRIM_MAX )
	    oops ( "Trim buffer too small" );
	memcpy ( &trim_buf[ntrim], buf, n );
	ntrim += n;
	// printf ( "Trim append %d %d\n", n, ntrim );
}

int
trim_record ( void )
{
	int header;
	int nn;
	off_t pos;
	u8 buf[256];

	header = peek1 ();

	if ( header & H_COMP ) {
	    printf ( "Compressed record\n" );
	    oops ( "Not ready for compressed records" );
	} else if ( header & H_DEF ) {
	    pos = fit_tell ();
	    nn = definition_record ();
	    fit_seek ( pos );
	    readn ( buf, nn );
	    trim_append ( buf, nn );
	} else {
	    pos = fit_tell ();
	    nn = data_record ( &def, 0 );
	    fit_seek ( pos );
	    readn ( buf, nn );
	    trim_append ( buf, nn );
	}

	return nn;
}

char *trim_path = "trim.fit";
int trim_fd;

void
trim_file ( int arg_trim )
{
	struct fit_header hdr;
	int nio;
	int nrec;
	int fd;
	u16 crc = 0;

	open_fit ();

	/* Read and copy header */
	readn ( (u8 *)&hdr, sizeof(struct fit_header) );
	trim_append ( (u8 *) &hdr, sizeof(struct fit_header) );
	nio = hdr.f_len;

	// printf ( "Trim: %d data bytes expected\n", nio );

	while ( nio > 0 ) {
	    nrec = trim_record ();
	    nio -= nrec;
	}

	if ( nio != 0 ) {
	    printf ( "%d bytes left in file\n", nio );
	    oops ( "Buffalo stampede (trim)" );
	}

	/* Recalculate CRC values.
	 * Amazingly, the way this works is that you have something
	 * like our header with 12 bytes.  You calculate the CRC over
	 * those 12 bytes, then you append that value (in little
	 * endian order) to that (giving you 14 bytes).
	 * Then if you calculate the CRC over those 14 bytes, you get
	 * zero.
	 */

	/* Header CRC */
	// printf ( "Buffer CRC = %02x %02x\n", trim_buf[12], trim_buf[13] );
	crc = calc_crc ( trim_buf, 12 );
	memcpy ( &trim_buf[12], &crc, 2 );
	// printf ( "Buffer CRC = %02x %02x\n", trim_buf[12], trim_buf[13] );

	/* File CRC */
	crc = calc_crc ( trim_buf, ntrim );
	trim_append ( (u8 *) &crc, 2 );
	// crc = calc_crc ( trim_buf, ntrim );
	// printf ( "Final CRC = %04x\n", crc );

	fd = open ( trim_path, O_CREAT | O_WRONLY | O_TRUNC, 0644 );
	if ( fd < 0 )
	    oops ( "Cannot open output trim file" );
	trim_fd = fd;


	// printf ( "%d bytes in trim buffer\n", ntrim );
	write ( trim_fd, trim_buf, ntrim );

	close ( trim_fd );
	close ( fit_fd );
}

void
dump_file ( void )
{
	dump_level = 2;
	read_file ();
}

int rec_num = -1;

void
out_cmd ( int n )
{
	struct data *dp;

	dp = &data[n];
	printf ( "MC %.5f %.5f\n", dp->lon, dp->lat );
}

void
show_data ( void )
{
	int i;
	struct data *dp;

	for ( i=0; i<ndata; i++ ) {
	    dp = &data[i];
	    printf ( "%s %.5f %.5f %.2f %.1f %.1f %.1f\n",
		tstamp(dp->time), dp->lon, dp->lat, dp->alt, dp->temp, dp->speed, dp->distance );
	}
}

/* --------------------------------------------------------- */
/* --------------------------------------------------------- */

enum cmd { EXTRACT, DUMP, TRIM };

enum cmd cmd = EXTRACT;

void
usage ( void )
{
	oops ( "Usage: fit66 -e path" );
}

void
cmdline ( int argc, char **argv )
{
	char *p;

	while ( argc-- ) {
	    p = *argv;

	    /*
	    rec_num = atoi ( *argv );
	    printf ( "Record %d\n", rec_num );
	    out_cmd ( rec_num );
	    */
	    if ( *p == '-' && p[1] == 't' )
		cmd = TRIM;
	    if ( *p == '-' && p[1] == 'd' )
		cmd = DUMP;
	    if ( *p == '-' && p[1] == 'e' )
		cmd = EXTRACT;

	    argv++;
	}
}

#define NTRIM	442

int
main ( int argc, char **argv )
{
	argc--;
	argv++;

	cmdline ( argc, argv );

	if ( cmd == DUMP ) {
	    dump_file ();
	    return 0;
	}

	if ( cmd == EXTRACT ) {
	    read_file ();
	    show_data ();
	    return 0;
	}

	if ( cmd == TRIM ) {
	    trim_file ( NTRIM );
	    return 0;
	}

	usage ();

	// wgs_test ();

	return 0;
}

/* THE END */
