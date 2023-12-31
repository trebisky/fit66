fit66

July 18, 2023

This is a program, written in C,
to read FIT files from my Garmin 66i

I compile and run this on my linux system
(currently Fedora 38 -- x86_64)

There are 3 major modes:

* fit66 -d path -- dump the file for analysis
* fit66 -e path -- extract records as plain ascii
* fit66 -t path -- trim records from end of file

The g66i program (in python, see below) uses "fit -e" to extract data
from a fit file, which it then relays to my gtopo program for display.

---------------------

Here is a sort of mini FIT file tutorial.

FIT files are binary.  Mine say they contain data in little endian
byte order, but there is a flag for this and it could be different.
I have just assumed little endian and forged ahead.

A fit file consists of a header, followed by lots of data records,
and ending with a 2 byte CRC.  The header also has a CRC.

When you talk about FIT files, you talk about "records"
and "messages".  A good way to think about it is that after
the header, you have a bunch of records.  The header tells you
the total bytes in all the records and you use that to know
when you are at the end.

The records are of two types.
(Actually there are more, but I only see the two I am about to describe).
You get a description record that describes "messages" that follow.
Then you get one or more data records that are messages that the
description just desribed.  The size of both the description record
and the "message" records cannot be predicted ahead of time, but
is defined by the content of the description records.
All the data records for a given description are the same size.
A new description record can pop up at any time, indicating that
a different sort of message follows.

The first pair of records is the description/data pair for what they
call a "file ID" message.  After that, anything goes.

In the files I have looked at, the bulk of the file is what they call
"record" messages.  These are points along the track I recorded with
my GPS and contain timestamps, latitude, longitude, and such.
These are what I am interested in.

There are also "event" messages, which may be interesting also.

General FIT files have lots of fitness related stuff that does not
appear in the files from my Garmin 66i.

---------------------

The actual file I am using to test and develop this program has
the following structure:

- Header (16 bytes)
- file ID message
- file creator message
- device info message
- record messages (1175 of them)
- event messages (2 of them)
- lap message
- session message
- activity message
- CRC (2 bytes)

---------------------

Also included is g66i

This is a python script that acts as a GUI frontend to fit66.
Its main purpose is to relay data from the FIT files to my
"gtopo" map display program -- so if you don't have this, you
probably won't find it greatly useful.

It allows display of a given track on a 7.5 minute map, or any other scale,
but the 7.5 minute maps are generally the most interesting.

It also allows point by point display on the map, along with display
of numerical values on the GUI.

