# Fit file games  7-14-2023

all: fit66

fit66:	fit66.c
	cc -o fit66 fit66.c -lm

install: fit66 g66i
	cp fit66 /home/tom/bin
	cp g66i /home/tom/bin

carrie.gpx: carrie.fit
	gpsbabel -i garmin_fit -f carrie.fit -o gpx -F carrie.gpx

