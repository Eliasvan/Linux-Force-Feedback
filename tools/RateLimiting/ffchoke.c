#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define min( a, b )    ( ( (a) < (b)) ? (a) : (b) )
#define max( a, b )    ( ( (a) > (b)) ? (a) : (b) )

/* Number of bits for 1 unsigned char */
#define nBitsPerUchar          (sizeof(unsigned char) * 8)
/* Index=Offset of given bit in 1 unsigned char */
#define bitOffsetInUchar(bit)  ((bit)%nBitsPerUchar)
/* Index=Offset of the unsigned char associated to the bit
   at the given index=offset */
#define ucharIndexForBit(bit)  ((bit)/nBitsPerUchar)
/* Test the bit with given index=offset in an unsigned char array */
#define testBit(bit, array)    ((array[ucharIndexForBit(bit)] >> bitOffsetInUchar(bit)) & 1)



/* Here are the interesting parameters' default values */
unsigned long update_period = 20000;                      /*       20ms     */
int simultaneous_effects_amount = 4;          /* try 4 simultaneous effects, if possible */
int simultaneous_effects_burstmode = 1;                   /* bursts enabled */
int continually_change_efct_params = 1;                   /*  keep changin  */
unsigned long choke_salvo_duration = 2000000;             /*    2 seconds   */
unsigned long effect_duration = 2000;                     /*    2 seconds   */
int effect_type = 0;                                      /* Constant Force */
int compensate_delays = 0;                    /*   force fixed delay between each salvo  */
/* Corresponding extended cmd-line: "./ffchoke /dev/input/event0 20000us 2 1 1 2000000us 2000ms 0 0" */

unsigned long safe_update_period = 50000; /* Used when we're not yet performing the choke test: 50ms */



#define N_EFFECTS 4

char* effect_names[] = {
	"Constant Force",
	"Sine Vibration",
	"Spring Condition",
	"Strong and Weak Rumble",
};

struct ff_effect effects[N_EFFECTS];

void init_effects()
{
	/* constant effect */
	memset(&effects[0], 0, sizeof(effects[0]));
	effects[0].type = FF_CONSTANT;
	effects[0].direction = 0x0000;	/* Along Y axis */

	/* periodic sinusoidal effect */
	memset(&effects[1], 0, sizeof(effects[1]));
	effects[1].type = FF_PERIODIC;
	effects[1].u.periodic.waveform = FF_SINE;
	effects[1].u.periodic.period = 1000;	/* 1 second */
	effects[1].direction = 0xC000;	/* Along X axis */

	/* condition spring effect */
	memset(&effects[2], 0, sizeof(effects[2]));
	effects[2].type = FF_SPRING;
	effects[2].u.condition[0].right_saturation = 0xFFFF;	/* No clipping */
	effects[2].u.condition[0].left_saturation = 0xFFFF;	/* No clipping */
	effects[2].u.condition[0].deadband = 0x0;
	effects[2].u.condition[0].center = 0x0;
	effects[2].u.condition[1] = effects[2].u.condition[0];

	/* a rumbling effect */
	memset(&effects[3], 0, sizeof(effects[3]));
	effects[3].type = FF_RUMBLE;
}



#define MAX_N_EFFECT_SLOTS 16

struct ff_effect effect_slots[MAX_N_EFFECT_SLOTS];

void update_effect_slot_parameters(int i, unsigned long progress_counter)
{
	switch (effect_type) {
	case 0:
		effect_slots[i].u.constant.level = 0x7FFF - progress_counter/2;
		return;
	case 1:
		effect_slots[i].u.periodic.magnitude = 0x7FFF - progress_counter/2;
		return;
	case 2:
		effect_slots[i].u.condition[0].right_coeff = 0x7FFF - progress_counter/2;
		effect_slots[i].u.condition[0].left_coeff = 0x7FFF - progress_counter/2;
		effect_slots[i].u.condition[1] = effect_slots[i].u.condition[0];
		return;
	case 3:
		effect_slots[i].u.rumble.strong_magnitude = 0xFFFF - progress_counter;
		effect_slots[i].u.rumble.weak_magnitude = 0xFFFF - progress_counter;
		return;
	}
}



unsigned long get_utime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	unsigned long time_in_micros = 1000000 * tv.tv_sec + tv.tv_usec;
	return time_in_micros;
}

int fd;
unsigned char ffFeatures[1 + FF_MAX/8/sizeof(unsigned char)];

/*
 * @option :
 *     1) Start an effect once, and repeatedly update it at the choke update-rate, during choke-salvo-duration-secs
 *     2) Repeatedly start an effect at the choke update-rate, during choke-salvo-duration-secs
 *     3) Repeatedly set the gain at the choke update-rate, during choke-salvo-duration-secs
 *     4) Repeatedly set the autocenter at the choke update-rate, during choke-salvo-duration-secs
 */
void handle_option(int option, int effect_idx)
{
	int i;
	int upload_and_start_without_delay_inbetween;
	unsigned long start_time, current_time, update_time, stop_time;
	unsigned long progress_counter;
	unsigned long n_updates;
	struct input_event ie;
	
	memset(&ie, 0, sizeof(ie));
	ie.type = EV_FF;
	
	if (option == 1 || option == 2) {
		if (!testBit(effects[effect_idx].type, ffFeatures)) {
			printf("This effect type is not supported by this device.\n");
			return;
		}
		
		/* Upload effects, and initialize at maximum strength/magnitude */
		for (i = 0; i < simultaneous_effects_amount; i++) {
			memcpy(&effect_slots[i], &effects[effect_idx], sizeof(effects[effect_idx]));
			effect_slots[i].id = -1;
			effect_slots[i].replay.length = effect_duration;
			update_effect_slot_parameters(i, 0);
			
			if (ioctl(fd, EVIOCSFF, &effect_slots[i]) < 0) {
				perror("Upload effect error");
			}
			
			usleep(safe_update_period);
		}
		printf("Uploaded all effects, ready to start them.\n");
		
		/* Start effects */
		for (i = 0; i < simultaneous_effects_amount; i++) {
			ie.code = effect_slots[i].id;
			ie.value = 1;
			
			printf("Starting '%s' with id: %d\n", effect_names[effect_idx], effect_slots[i].id);
			if (write(fd, &ie, sizeof(ie)) < 0) {
				perror("Play effect error");
				exit(1);
			}
			
			usleep(safe_update_period);
		}
		printf("Started all effects, ready to choke.\n");
		
		/* Warn about some special case */
		upload_and_start_without_delay_inbetween = (option == 2 && continually_change_efct_params);
		if (upload_and_start_without_delay_inbetween) {
			printf("Warning: because the effect-parameters have to be changed on each update,\n");
			printf("\tand because you've chosen to restart the effects on each update,\n");
			printf("\tan 'upload' will be performed directly before a 'start',\n");
			printf("\tand the average update-period (reported at the end) will not take this into account.\n");
		}
		
		/* Wait 1 second before starting the choking, to be able to differentiate from setup msgs in dmesg */
		printf("Waiting 1 second to make it easier to differentiate between dmesg timestamps...\n");
		usleep(1e6);
	}
	else if (option == 3) {
		if (!testBit(FF_GAIN, ffFeatures)) {
			printf("Setting gain is not supported by this device.\n");
			return;
		}
		
		/* Set master gain to 100% */
		ie.code = FF_GAIN;
		ie.value = 0xFFFF;
	}
	else if (option == 4) {
		if (!testBit(FF_AUTOCENTER, ffFeatures)) {
			printf("Setting autocenter is not supported by this device.\n");
			return;
		}
		
		/* Set autocenter to 100% */
		ie.code = FF_AUTOCENTER;
		ie.value = 0xFFFF;
	}
	
	printf("\nStarted the choke-test...\n");
	
	start_time = get_utime();
	update_time = start_time;
	current_time = start_time;
	n_updates = 0;
	i = 0;
	
	while (current_time - start_time < choke_salvo_duration) {
		current_time = get_utime();
		progress_counter = max(0ul, min(0xFFFFul, 0xFFFFul * (current_time - start_time) / choke_salvo_duration));
		if (compensate_delays) {
			update_time += update_period;
			if (update_time > current_time)
				usleep(update_time - current_time);
		} else {
			usleep(update_period);
		}
		
		/* Choke-command-body */
		switch (option) {
		case 1:
		case 2:
			if (simultaneous_effects_burstmode) i = 0;
			for (; i < simultaneous_effects_amount; i++) {
				/* Upload after updating parameters, if wanted */
				if (option == 1 || upload_and_start_without_delay_inbetween) {
					if (continually_change_efct_params)
						update_effect_slot_parameters(i, progress_counter);
					
					if (ioctl(fd, EVIOCSFF, &effect_slots[i]) < 0) {
						perror("Upload effect error");
						exit(1);
					}
				}
				
				/* Start */
				if (option == 2) {
					ie.code = effect_slots[i].id;
					
					if (write(fd, &ie, sizeof(ie)) < 0) {
						perror("Write error");
						exit(1);
					}
				}
				
				if (!simultaneous_effects_burstmode) {
					i = (i+1) % simultaneous_effects_amount;
					break;
				}
			}
			break;
		case 3:
		case 4:
			if (continually_change_efct_params)
				ie.value = 0xFFFF - progress_counter;
			
			if (write(fd, &ie, sizeof(ie)) < 0) {
				perror("Write error");
				exit(1);
			}
			break;
		}
		
		n_updates++;
	}
	
	/* Report statistics */
	stop_time = get_utime();
	if (n_updates)
		printf("Done, average update-period was %luus.\n", (stop_time - start_time) / n_updates);
	else
		printf("Failed to send any update.\n");
	
	if (option == 1 || option == 2) {
		/* Wait 1 second before stopping and removing effects, to be able to differentiate from setup msgs in dmesg */
		usleep(1e6);
		printf("\nInfo: I again waited during 1 second to make it easier to differentiate between dmesg timestamps.\n");
		
		/* Stop the effects */
		for (i = 0; i < simultaneous_effects_amount; i++) {
			usleep(safe_update_period);
			
			ie.code = effect_slots[i].id;
			ie.value = 0;
			
			if (write(fd, &ie, sizeof(ie)) < 0) {
				perror("Stop effect error");
				exit(1);
			}
		}
		
		/* Remove the effects */
		for (i = 0; i < simultaneous_effects_amount; i++) {
			usleep(safe_update_period);
			
			if (ioctl(fd, EVIOCRMFF, effect_slots[i].id) < 0) {
				perror("Remove effect error");
				exit(1);
			}
		}
		
		printf("Stopped and Removed all effects, done.\n");
	}
}

int main(int argc, char** argv)
{
	const char * device_file_name = "/dev/input/event0";
	int i, j;
	
	printf("Force feedback test program to choke a device(-driver) with commands.\n");
	printf("HOLD FIRMLY YOUR WHEEL OR JOYSTICK TO PREVENT DAMAGES\n\n");
	
	/* Show help message */
	for (i = 1; i < argc; i++) {
		if (strncmp(argv[i], "--help", 64) == 0) {
			printf("Usage: %s [/dev/input/eventXX \n", argv[0]);
			printf("           \t\t[<update_period=%luus> \n", update_period);
			printf("           \t\t[<simultaneous_effects_amount=%d> \n", simultaneous_effects_amount);
			printf("           \t\t[<simultaneous_effects_burstmode=%d> \n", simultaneous_effects_burstmode);
			printf("           \t\t[<continually_change_efct_params=%d> \n", continually_change_efct_params);
			printf("           \t\t[<choke_salvo_duration=%luus> \n", choke_salvo_duration);
			printf("           \t\t[<effect_duration=%lums> \n", effect_duration);
			printf("           \t\t[<effect_type=%d> \n", effect_type);
			printf("           \t\t[<compensate_delays=%d> \n", compensate_delays);
			printf("           ]]]]]]] ]\n");
			printf("Tests the ratelimiting of the force feedback driver, check dmesg for USB buffer overruns\n\n");
			
			printf("Global mode of operation:\n");
			printf("I will perform the action defined by the chosen option (in the interactive menu)\n");
			printf("at a rate defined by 'update_period', during a time of 'choke_salvo_duration'.\n");
			printf("In case of sending effects, I will send an amount of 'simultaneous_effects_amount' multiple effects at once,\n");
			printf("these effects will (should) be identical to each-other.\n");
			printf("And if 'continually_change_efct_params' is set to '1' instead of '0',\n");
			printf("I will let the action's magnitude parameters linearly decrease from max to zero.\n\n");
			
			printf("Additional details on some parameters:\n");
			printf("\tupdate_period:\t (to choke), in microseconds\n");
			printf("\tsimultaneous_effects_amount:\t a number from '0' to max('%d', <max number defined by device & driver>)\n", MAX_N_EFFECT_SLOTS);
			printf("\tsimultaneous_effects_burstmode:\n");
				printf("\t\tif '1', all simultaneous effects will be sent in one update,\n");
				printf("\t\taverage update-period (reported at end of choke-test) will not take this into account;\n");
				printf("\t\totherwise when '0', we insert an amount 'update_period' of sleep-time in-between.\n");
			printf("\tchoke_salvo_duration:\t in microseconds\n");
			printf("\teffect_duration:\t the duration of an effect, in milliseconds\n");
			printf("\teffect_type:\t the type id of an effect, should be one of the following:\n");
				for (j=0; j<N_EFFECTS; ++j) printf("\t\t%d: %s\n", j, effect_names[j]);
			printf("\tcompensate_delays:\n");
				printf("\t\tif '1', deadlines are forced to be achieved,\n");
				printf("\t\totherwise when '0', we *always* sleep an amount 'update_period' of time between updates;\n");
				printf("\t\tremember that most real simulation-games will have this set to '1' instead of '0'.\n\n");
			
			printf("Example (extended) usage: '%s %s %luus %d %d %d %luus %lums %d %d'\n",
					argv[0], device_file_name, update_period, simultaneous_effects_amount, simultaneous_effects_burstmode,
					continually_change_efct_params, choke_salvo_duration, effect_duration, effect_type, compensate_delays);
				printf("\t(this corresponds to the default parameters)\n");
			
			exit(1);
		}
	}
	
	/* Parse cmd arguments and set parameters */
	i=1; if (argc > i) device_file_name               = argv[i];
	i++; if (argc > i) update_period                  = atoi(argv[i]);
	i++; if (argc > i) simultaneous_effects_amount    = atoi(argv[i]);
	i++; if (argc > i) simultaneous_effects_burstmode = atoi(argv[i]);
	i++; if (argc > i) continually_change_efct_params = atoi(argv[i]);
	i++; if (argc > i) choke_salvo_duration           = atoi(argv[i]);
	i++; if (argc > i) effect_duration                = atoi(argv[i]);
	i++; if (argc > i) effect_type                    = atoi(argv[i]);
	i++; if (argc > i) compensate_delays              = atoi(argv[i]);
	
	/* Open device */
	printf("Opening %s ...\n", device_file_name);
	fd = open(device_file_name, O_RDWR);
	if (fd == -1) {
		perror("Open device file");
		exit(1);
	}
	printf("Device opened\n");
	
	/* Force feedback effects */
	memset(ffFeatures, 0, sizeof(ffFeatures)*sizeof(unsigned char));
	if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ffFeatures)*sizeof(unsigned char)), ffFeatures) == -1) {
		perror("Ioctl force feedback features query");
		exit(1);
	}
	
	/* Number of effects the device can play at the same time */
	printf("Info: Maximum number of simultaneous effects: ");
	int n_effects;
	if (ioctl(fd, EVIOCGEFFECTS, &n_effects) == -1) {
		perror("Ioctl number of effects");
	}
	printf("%d\n", n_effects);
	if (simultaneous_effects_amount > max(MAX_N_EFFECT_SLOTS, n_effects)) {
		printf("Warning: A too high simultaneous_effects_amount was set, I'll set it to the maximum (%d) instead.\n", max(MAX_N_EFFECT_SLOTS, n_effects));
		simultaneous_effects_amount = max(MAX_N_EFFECT_SLOTS, n_effects);
	}
	
	init_effects();
	
	/* Ask user what options to execute */
	do {
		printf("---\n\nOptions:\n");
		printf("\t0) Set parameters\n\t");
			printf("\t0. update_period=%luus;", update_period);
			printf("\t1. simultaneous_effects_amount=%d;", simultaneous_effects_amount);
			printf("\t2. simultaneous_effects_burstmode=%d;", simultaneous_effects_burstmode);
			printf("\t3. continually_change_efct_params=%d;", continually_change_efct_params);
			printf("\t4. choke_salvo_duration=%luus;", choke_salvo_duration);
			printf("\t5. effect_duration=%lums;", effect_duration);
			printf("\t6. effect_type=%d;", effect_type);
			printf("\t7. compensate_delays=%d;", compensate_delays);
			printf("\n");
		float choke_salvo_duration_secs = ((float)choke_salvo_duration) / 1e6;
		printf("\t1) Start an effect once, and repeatedly update it at the choke update-rate, during %.3f second(s)\n", choke_salvo_duration_secs);
		printf("\t2) Repeatedly start an effect at the choke update-rate, during %.3f second(s)\n", choke_salvo_duration_secs);
		printf("\t3) Repeatedly set the gain at the choke update-rate, during %.3f second(s)\n", choke_salvo_duration_secs);
		printf("\t4) Repeatedly set the autocenter at the choke update-rate, during %.3f second(s)\n", choke_salvo_duration_secs);
		
		printf("Enter option number, -1 to exit\n");
		i = -1;
		if (scanf("%d", &i) == EOF) {
			printf("Read error\n");
		}
		else if (i == 0) {
			
			/* Ask user what parameter to change */
			do {
				printf("For more details on the parameters, restart this program while passing the '--help' cmd argument.\n");
				
				printf("Enter parameter id to change, -1 to exit\n");
				j = -1;
				if (scanf("%d", &j) == EOF) {
					printf("Read error\n");
				}
				else if (j >= 0 && j <= 7) {
					printf("Enter new value of that parameter: ");
					if      (j == 0) {if (scanf("%lu", &update_period                 ) == EOF) printf("Read error\n");}
					else if (j == 1) {if (scanf("%d",  &simultaneous_effects_amount   ) == EOF) printf("Read error\n");}
					else if (j == 2) {if (scanf("%d",  &simultaneous_effects_burstmode) == EOF) printf("Read error\n");}
					else if (j == 3) {if (scanf("%d",  &continually_change_efct_params) == EOF) printf("Read error\n");}
					else if (j == 4) {if (scanf("%lu", &choke_salvo_duration          ) == EOF) printf("Read error\n");}
					else if (j == 5) {if (scanf("%lu", &effect_duration               ) == EOF) printf("Read error\n");}
					else if (j == 6) {if (scanf("%d",  &effect_type                   ) == EOF) printf("Read error\n");}
					else if (j == 7) {if (scanf("%d",  &compensate_delays             ) == EOF) printf("Read error\n");}
					
					if (j == 6 && !(effect_type >= 0 && effect_type < N_EFFECTS))
						printf("Warning: You set an invalid effect_type.\n");
					else if (j == 1 && simultaneous_effects_amount > max(MAX_N_EFFECT_SLOTS, n_effects)) {
						simultaneous_effects_amount = max(MAX_N_EFFECT_SLOTS, n_effects);
						printf("Warning: You set a too high simultaneous_effects_amount, I set it to the maximum (%d) instead.\n", max(MAX_N_EFFECT_SLOTS, n_effects));
					}
					
					break;
				}
				else if (j != -1) {
					printf("No such parameter\n");
				}
			} while (j >= 0);
			
		}
		else if (i == 1 || i == 2) {
			handle_option(i, effect_type);
		}
		else if (i == 3 || i == 4) {
			handle_option(i, -1);
		}
		else if (i != -1) {
			printf("No such option\n");
		}
	} while (i >= 0);
	
	exit(0);
}
