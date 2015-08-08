/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/input.h>

enum {
	FORCE_DEV,
	RUMBLE_DEV
} device_type;

void print_help(char *device_file_name, unsigned long update_period, unsigned long total_time)
{
	printf("Usage: %s /dev/input/eventXX [updatePeriodMicros (default=%d) [totalTimeMicros (default=%d)]]\n",
			device_file_name, update_period, total_time);
	printf("Tests the force feedback driver\n");
	exit(1);
}

int main(int argc, char **argv)
{
	struct ff_effect effect;
	struct input_event play, stop;
	unsigned long update_period, total_time;
	int fd;
	char device_file_name[64];
	int i;

	printf("Force feedback test program.\n");
	printf("HOLD FIRMLY YOUR WHEEL OR JOYSTICK TO PREVENT DAMAGES\n\n");

	strncpy(device_file_name, "/dev/input/event0", 64);
	update_period = 1000; /* 1 ms */
	total_time = 5000000; /* 5 s */

	/* Parse command-line arguments */
	if (argc == 1)
		print_help(argv[0], update_period, total_time);
	for (i = 1; i < argc; ++i) {
		if (strncmp(argv[i], "--help", 64) == 0)
			print_help(argv[0], update_period, total_time);
		switch (i) {
		case 1:
			strncpy(device_file_name, argv[i], 64);
			break;
		case 2:
			update_period = atol(argv[i]);
			break;
		case 3:
			total_time = atol(argv[i]);
			break;
		}
	}

	/* Open device */
	fd = open(device_file_name, O_RDWR);
	if (fd == -1) {
		perror("Open device file");
		exit(1);
	}
	printf("Device %s opened\n", device_file_name);

	/* Upload a constant effect (force devices) */
	device_type = FORCE_DEV;
	memset(&effect, 0, sizeof(effect));
	effect.type = FF_CONSTANT;
	effect.id = -1;
	effect.direction = 0x6000;	/* 135 degrees */

	if (ioctl(fd, EVIOCSFF, &effect) < 0) {
		/* Upload a rumble effect (rumble devices) */
		device_type = RUMBLE_DEV;
		memset(&effect, 0, sizeof(effect));
		effect.type = FF_RUMBLE;
		effect.id = -1;
		
		if (ioctl(fd, EVIOCSFF, &effect) < 0) {
			perror("Upload effect");
			exit(1);
		}
	}

	/* Play the effect */
	play.type = EV_FF;
	play.code = effect.id;
	play.value = 1;

	if (write(fd, (const void*) &play, sizeof(play)) == -1) {
		perror("Play effect");
		exit(1);
	}

	/* Attempt to choke the device by sending bogus effects (with low magnitude) 
	 * at a rate higher than the device can handle */
	printf("Now Playing CONSTANT/RUMBLE effect with almost unnoticable magnitude...\n");

	for (i = 0; i < total_time / update_period; ++i) {
		usleep(update_period);

		switch (device_type) {
		case (FORCE_DEV):
			effect.u.constant.level = i % 2;
			break;
		case (RUMBLE_DEV):
			effect.u.rumble.strong_magnitude = i % 2;
			effect.u.rumble.weak_magnitude = i % 2;
			break;
		}

		if (ioctl(fd, EVIOCSFF, &effect) < 0) {
			perror("Upload effect");
			exit(1);
		}
	}

	/* Send the actual useful effect,
	 * if the driver works properly, this one should be noticed almost immediately,
	 * i.e. there should be no lag */
	printf("Now Playing CONSTANT/RUMBLE effect with large magnitude...\n");

	switch (device_type) {
	case (FORCE_DEV):
		effect.u.constant.level = 0x7FFF;
		break;
	case (RUMBLE_DEV):
		effect.u.rumble.strong_magnitude = 0xFFFF;
		effect.u.rumble.weak_magnitude = 0xFFFF;
		break;
	}

	if (ioctl(fd, EVIOCSFF, &effect) < 0) {
		perror("Upload effect");
		exit(1);
	}

	usleep(total_time);

	/* Stop the effect */
	stop.type = EV_FF;
	stop.code = effect.id;
	stop.value = 0;

	if (write(fd, (const void*) &stop, sizeof(stop)) == -1) {
		perror("Stop effect");
		exit(1);
	}

	printf("The CONSTANT/RUMBLE effect has been stopped.\n");

	usleep(total_time);

	/* Clear the effect */
	if (ioctl(fd, EVIOCRMFF, effect.id) < 0) {
		perror("Erase effect");
		exit(1);
	}

	exit(0);
}
