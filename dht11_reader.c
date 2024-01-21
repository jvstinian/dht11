// file input.c
#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include "getopt.h"

static const char *prog_name = "dht11_reader";

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

struct config {
  unsigned int line_offset;
  unsigned int hold_period_us;
  bool verbose;
  unsigned int chip_number;
};

// The following is taken from 
// https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/tree/tools/tools-common.c
void print_period_help(void)
{
        printf("\nPeriods:\n");
        printf("    Periods are taken as milliseconds unless units are specified. e.g. 10us.\n");
        printf("    Supported units are 's', 'ms', and 'us'.\n");
}

static void print_help(void)
{
        printf("Usage: %s [OPTIONS] \n", prog_name);
        printf("\n");
        printf("Read DHT11 sensor.\n");
        printf("\n");
        printf("Options:\n");
        printf("  -l, --line <offset>           line offset for the DHT11 signal              default: 17\n");
        printf("  -p, --hold-period <period>    time period to hold low to initiate sensor    default: 180\n");
        printf("  -c, --chip <chip>             restrict scope to a particular chip           default: 0\n");
        printf("  -v, --verbose                 print info for debugging\n");
        printf("  -h, --help                    display this help and exit\n");
        print_period_help();
        printf("\n");
}

// The following is taken from 
// https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/tree/tools/tools-common.c
unsigned int parse_period(const char *option)
{
        unsigned long p, m = 0;
        char *end;

        p = strtoul(option, &end, 10);

        switch (*end) {
        case 'u':
                m = 1;
                end++;
                break;
        case 'm':
                m = 1000;
                end++;
                break;
        case 's':
                m = 1000000;
                break;
        case '\0':
                break;
        default:
                return -1;
        }

        if (m) {
                if (*end != 's')
                        return -1;

                end++;
        } else {
                m = 1000;
        }

        p *= m;
        if (*end != '\0' || p > INT_MAX)
                return -1;

        return p;
}

// The following is taken from 
// https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/tree/tools/tools-common.c
unsigned int parse_period_or_die(const char *option)
{
        int period = parse_period(option);

        if (period < 0) {
                fprintf(stderr, "invalid period: %s", option);
                exit(EXIT_FAILURE);
        }

        return period;
}

unsigned int parse_line_or_die(const char *option)
{
        int line = atoi(option);

        if (line <= 0) {
                fprintf(stderr, "invalid line: %s", option);
                exit(EXIT_FAILURE);
        }

        return (unsigned int) line;
}

unsigned int parse_chip_number_or_die(const char *option)
{
        int chip_number = atoi(option);

        if (chip_number <= 0) {
                fprintf(stderr, "invalid chip number: %s", option);
                exit(EXIT_FAILURE);
        }

        return (unsigned int) chip_number;
}

static int initialize_config(struct config *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->line_offset = 17;
  cfg->hold_period_us = 180;
  cfg->verbose = false;
  // cfg->chip_id = "0";
  cfg->chip_number = 0;
}

static int parse_config(int argc, char **argv, struct config *cfg)
{
        static const struct option longopts[] = {
                { "line",         required_argument, NULL, 'l' },
                { "hold-period",  required_argument, NULL, 'p' },
                { "chip",         required_argument, NULL, 'c' },
                { "verbose",            no_argument, NULL, 'v' },
                { "help",               no_argument, NULL, 'h' },
                { NULL,                           0, NULL,  0  },
        };

        static const char *const shortopts = "l:p:c:vh";

        int opti, optc;

        while ((optc = getopt_long(argc, argv, shortopts, longopts, &opti)) >= 0) {
                switch (optc) {
                case 'l':
                        cfg->line_offset = parse_line_or_die(optarg);
                        break;
                case 'p':
                        cfg->hold_period_us = parse_period_or_die(optarg);
                        break;
                case 'c':
                        cfg->chip_number = parse_chip_number_or_die(optarg);
                        break;
                case 'v':
                        cfg->verbose = true;
                        break;
                case 'h':
                        print_help();
                        exit(EXIT_SUCCESS);
                case 0:
                        break;
                case '?':
                        fprintf(stderr, "try %s --help", prog_name);
                        exit(EXIT_FAILURE);
                default:
                        fprintf(stderr, "encountered unexpected issue parsing options, try %s --help", prog_name);
                        exit(EXIT_FAILURE);
                }
        }

        return optind;
}

void cleanup(
  struct gpiod_chip *chip,
  struct gpiod_line * line,
  struct gpiod_line_event *events 
) {
  if (events != NULL) {
    free(events);
    events = NULL;
  }
  if (line != NULL)
    gpiod_line_release(line);
  gpiod_chip_close(chip);
}

int main(int argc, char *argv[])
{
  struct config cfg;
  
  initialize_config(&cfg);
  parse_config(argc, argv, &cfg);
  if (cfg.verbose) {
    printf("line offset: %d\n", cfg.line_offset);
    printf("hold period: %d\n", cfg.hold_period_us);
    // printf("chip_id: %s\n", cfg.chip_id);
    printf("chip_number: %d\n", cfg.chip_number);
    printf("verbose: %d\n", cfg.verbose);
  }

  unsigned int offset = cfg.line_offset;

  int err;
  
  struct gpiod_chip *chip;
  struct gpiod_line * line = NULL;
  int total_events = 84;
  struct gpiod_line_event *events = (struct gpiod_line_event*) malloc(total_events * sizeof(struct gpiod_line_event));

  chip = gpiod_chip_open_by_number(cfg.chip_number);
  if(!chip)
  {
    perror("gpiod_chip_open");
    gpiod_chip_close(chip);
    // free(device_chip_path);
    exit(EXIT_FAILURE);
  }

  line = gpiod_chip_get_line(chip, offset);
  if(line == NULL)
  {
    perror("gpiod_chip_get_line");
    cleanup(chip, line, events);
    exit(EXIT_FAILURE);
  }

  const int highval = 1; 
  const int lowval = 0;
  // We request to use the line for output, and set the value to HIGH
  err = gpiod_line_request_output(line, "initialize-reading", highval);
  if(err == -1)
  {
    perror("gpiod_line_request_output");
    cleanup(chip, line, events);
    exit(EXIT_FAILURE);
  }
  else if(err == 0)
  {
    if (cfg.verbose) 
      fprintf(stdout, "Set line to output.\n");
  }
  // We set the pin to LOW for the hold period.
  // The default is is 180 microseconds.
  // Note that the documentation indicates that the LOW 
  // value should be maintained for 18 milliseconds, or 
  // 18000 microseconds.  However, we found that if we 
  // used 18 milliseconds, the sensor started returning 
  // values which we missed.  We found that we received the 
  // expected signals if we set the pin to LOW for 180 microseconds.
  gpiod_line_set_value(line, lowval);
  usleep(cfg.hold_period_us);
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
    cleanup(chip, line, events);
    exit(EXIT_FAILURE);
  }
  else 
  {
    if (cfg.verbose) 
      fprintf(stdout, "Setting up line request for both edges events, return %d\n", err);
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
      cleanup(chip, line, events);
      exit(EXIT_FAILURE);
    }
    num_events += err;
    eventsptr += err;
  }

  if (cfg.verbose) {
    for (int idx = 0; idx < num_events; idx++) {
            fprintf(stdout, "Event type: %d, time %lld.%.9ld", events[idx].event_type, (long long) events[idx].ts.tv_sec, events[idx].ts.tv_nsec);
            if (idx > 0) {
              struct timespec tdiff = timespec_diff(&(events[idx-1].ts), &(events[idx].ts));
              fprintf(stdout, ", time diff %lld.%.9ld", (long long) tdiff.tv_sec, tdiff.tv_nsec);
              unsigned int usecdiff = tdiff.tv_nsec / 1000;
              fprintf(stdout, " or %.6u", usecdiff);
              fprintf(stdout, "\n");
            } else {
              fprintf(stdout, "\n");
	    }
    }
  }
  
  // performing checks
  int expected_val = 2;
  bool success = true;
  for (int idx = 0; idx < num_events; idx++) {
    if(events[idx].event_type != expected_val) { 
      fprintf(stderr, "For idx %d, got event type %d, expected %d\n", idx, events[idx].event_type, expected_val);
      success = false;
      break;
    }
    expected_val = (expected_val == 1) ? 2 : 1;
  }
  if (success) { 
    if (cfg.verbose) { 
      printf("The data was read successfully.\n");
     }
  } else {
    fprintf(stderr, "There was an error reading the data.\n");
    cleanup(chip, line, events);
    exit(EXIT_FAILURE);
  }

  unsigned int vals[5];
  for (int idx = 0; idx < 5; idx++) {
    vals[idx] = 0u;
  }

  time_t now = time(NULL);

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

  // bit check
  if ((vals[0] + vals[1] + vals[2] + vals[3]) != vals[4]) {
    fprintf(stderr, "The bit check failed\n");
    cleanup(chip, line, events);
    exit(EXIT_FAILURE);
  } else {
    if (cfg.verbose)
      printf("The bit check succeeded\n");
  }

  float humidity = ((float) vals[0]) + 0.1 * ((float) vals[1]);
  float temperature = ((float) vals[2]) + 0.1 * ((float) vals[3]);
  if (cfg.verbose) {
    char timeiso8601[21];
    strftime(timeiso8601, 21, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    printf("Values: %d, %d, %d, %d\n", vals[0], vals[1], vals[2], vals[3]);
    printf("Time: %s\n", timeiso8601);
    printf("Humidity: %f\n", humidity);
    printf("Temperature: %f Celsius\n", temperature);
    printf("Temperature: %f Fahrenheit\n", 1.8 * temperature + 32.0);
  } else {
    printf("%ld,%f,%f\n", now, humidity, temperature);
  }

  cleanup(chip, line, events);
  return EXIT_SUCCESS;
}

