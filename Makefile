# Fit file games  7-14-2023

all: fit66

fit66:	fit66.c
	cc -o fit66 fit66.c -lm

install: fit66 g66i
	cp fit66 /home/tom/bin
	cp g66i /home/tom/bin

carrie.gpx: carrie.fit
	gpsbabel -i garmin_fit -f carrie.fit -o gpx -F carrie.gpx

#LATEST = '2023-09-13 16.08.29.fit'
LATEST = '2023-09-18 20.58.35.fit'

new:
	cp /u1/Projects/Garmin/Files/Activities/$(LATEST) new.fit

# THE END

