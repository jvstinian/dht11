// file input.c
#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
/*
#include <error.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
*/

struct gpiod_chip *chip;

/* timespec_diff is a method for taking the 
 * difference between two timespecs.  
 * Note that stop >= start, 
 * otherwise the resulting timespec will not be valid.
 */
struct timespec timespec_diff(
	struct timespec *start, 
	struct timespec *stop
)
{
    struct timespec result;
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result.tv_sec = stop->tv_sec - start->tv_sec - 1;
        result.tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result.tv_sec = stop->tv_sec - start->tv_sec;
        result.tv_nsec = stop->tv_nsec - start->tv_nsec;
    }

    return result;
}

int
main(int argc, char *argv[])
{
  unsigned int offset;

  int err;

  chip = gpiod_chip_open("/dev/gpiochip0");
  if(!chip)
  {
    perror("gpiod_chip_open");
    goto cleanup;
  }

  offset = 17;
	
  struct gpiod_line * line;
  line = gpiod_chip_get_line(chip, offset);
  if(line == NULL)
  {
    perror("gpiod_chip_get_line");
    goto cleanup;
  }

  int total_events = 84;
  struct gpiod_line_event *events = (struct gpiod_line_event*) malloc(total_events * sizeof(struct gpiod_line_event));

  int highval = 1; 
  int lowval = 0;
  // We request to use the line for output, and set the value to HIGH
  err = gpiod_line_request_output(line, "initialize-reading", highval);
  if(err == -1)
  {
    perror("gpiod_line_request_output");
    goto cleanup; // TODO: Check this
  }
  else if(err == 0)
  {
    fprintf(stdout, "Set line to output.");
  }
  // We set the pin to LOW for 180 microseconds.
  // Note that the documentation indicates that the LOW 
  // value should be maintained for 18 milliseconds, or 
  // 18000 microseconds.  However, we found that if we 
  // used 18 milliseconds, the sensor started returning 
  // values which we missed.  We found that we received the 
  // expected signals if we set the pin to LOW for 180 microseconds.
  gpiod_line_set_value(line, lowval);
  usleep(180);
  // The documentation says to set the pin value to HIGH before 
  // switching to input, however we are under the impression that 
  // on releasing the output request that the pin will automatically 
  // be pulled to HIGH.  
  // For this reason we do not currently explicitly set the pin 
  // value to HIGH.  This appears to work.
  gpiod_line_release(line);

  // We request to read both (i.e., falling and rising) edge events
  err = gpiod_line_request_both_edges_events(line, "read-device-output");
  if(err == -1)
  {
    perror("gpiod_line_request_both_edges_events");
    goto cleanup; // TODO: Check this
  }
  else 
  {
    // TODO: Consider printing the following if verbosity is set
    // fprintf(stdout, "Setting up line request for both edges events, return %d", err);
  }

  int num_events = 0;
  struct gpiod_line_event *eventsptr = events; 
  // Unfortunately reading all events at one time doesn't appear to be possible 
  // (and might actually not be possible given limitations within the libgpiod code), 
  // and so we make multiple reads until we've read the expected events.
  while (num_events < total_events) {
    err = gpiod_line_event_read_multiple(line, eventsptr, total_events - num_events);
    if(err == -1)
    {
      perror("gpiod_line_event_read_multiple");
      goto cleanup; // TODO: Check this
    }
    else 
    {
      // fprintf(stdout, "\nRead events, return %d", err);
    }
    num_events += err;
    eventsptr += err;
  }

  for (int idx = 0; idx < num_events; idx++) {
	  fprintf(stdout, "\nEvent type: %d, time %lld.%.9ld", events[idx].event_type, (long long) events[idx].ts.tv_sec, events[idx].ts.tv_nsec);
	  if (idx > 0) {
		struct timespec tdiff = timespec_diff(&(events[idx-1].ts), &(events[idx].ts));
	  	fprintf(stdout, ", time diff %lld.%.9ld", (long long) tdiff.tv_sec, tdiff.tv_nsec);
		unsigned int usecdiff = tdiff.tv_nsec / 1000;
	  	fprintf(stdout, " or %.6u", usecdiff);
	  }
  }
  
  // performing checks
  int expected_val = 2;
  bool success = true;
  for (int idx = 0; idx < num_events; idx++) {
    if(events[idx].event_type != expected_val) { 
      fprintf(stderr, "\nFor idx %d, got event type %d, expected %d", idx, events[idx].event_type, expected_val);
      success = false;
      break;
    }
    expected_val = (expected_val == 1) ? 2 : 1;
  }
  if (success) { 
    printf("\nThe data was read successfully.");
  } else {
    fprintf(stderr, "\nThere was an error reading the data.");
  }

  unsigned int vals[5];
  for (int idx = 0; idx < 5; idx++) {
    vals[idx] = 0u;
  }

  int one_usec_lb = (24 + 70) / 2;
  int base_idx = 4;
  for (int val_idx = 0; val_idx < 5; val_idx++) {
    for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
      int target_idx = base_idx + 2 * 8 * val_idx + 2 * bit_idx ;
      struct timespec tdiff = timespec_diff(&(events[target_idx-1].ts), &(events[target_idx].ts));
      int usecdiff = (int) (tdiff.tv_nsec / 1000);
      vals[val_idx] <<= 1;
      if (usecdiff > one_usec_lb) {
        vals[val_idx] |= 1u;
      } // else bit represents a 0
    }
  }

  free(events);

  // bit check
  if ((vals[0] + vals[1] + vals[2] + vals[3]) != vals[4]) {
    fprintf(stderr, "\nThe bit check failed");
  } else {
    printf("\nThe bit check succeeded");
  }

  float humidity = ((float) vals[0]) + 0.1 * ((float) vals[1]);
  float temperature = ((float) vals[2]) + 0.1 * ((float) vals[3]);
  printf("\nHumidity: %f", humidity);
  printf("\nTemperature: %f Celsius", temperature);
  printf("\nTemperature: %f Fahrenheit", 1.8 * temperature + 32.0);

cleanup:
  gpiod_line_release(line);
  gpiod_chip_close(chip);

  return EXIT_SUCCESS;
}
