/* CPE-2600 Systems Programming, Lab 11: Multiprocessing
  Generate a sequence of Mandelbrot frames using a bounded number of children. */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_FRAME_COUNT 50
#define DEFAULT_CENTER_X (-0.38)
#define DEFAULT_CENTER_Y (-0.665)
#define DEFAULT_INITIAL_SCALE 0.05
#define DEFAULT_ZOOM_FACTOR 0.90
#define DEFAULT_MAX_ITERATIONS 1000
#define DEFAULT_WIDTH 1000
#define DEFAULT_HEIGHT 1000

static void show_help(FILE *stream, const char *program_name)
{
	fprintf(stream,
		"Usage: %s -p <processes> [options]\n"
		"Generate a zoom sequence as mandel0.jpg through mandelN.jpg.\n\n"
		"Required option:\n"
		"  -p <count>       Maximum number of child processes (1 to frame count)\n\n"
		"Frame options:\n"
		"  -n <count>       Number of frames (default: %d)\n"
		"  -x <coordinate>  Center X coordinate (default: %.6g)\n"
		"  -y <coordinate>  Center Y coordinate (default: %.6g)\n"
		"  -s <scale>       Starting X scale (default: %.6g)\n"
		"  -z <factor>      Scale multiplier per frame, between 0 and 1 (default: %.6g)\n"
		"  -m <iterations>  Maximum iterations per pixel (default: %d)\n"
		"  -W <pixels>      Image width (default: %d)\n"
		"  -H <pixels>      Image height (default: %d)\n"
		"  -h               Show this help\n",
		program_name, DEFAULT_FRAME_COUNT, DEFAULT_CENTER_X, DEFAULT_CENTER_Y,
		DEFAULT_INITIAL_SCALE, DEFAULT_ZOOM_FACTOR, DEFAULT_MAX_ITERATIONS,
		DEFAULT_WIDTH, DEFAULT_HEIGHT);
}

static int parse_integer(const char *text, const char *option, int *value)
{
	char *end;
	long parsed;

	errno = 0;
	parsed = strtol(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || parsed < 1 || parsed > 1000000) {
		fprintf(stderr, "Invalid value for -%s: %s\n", option, text);
		return -1;
	}
	*value = (int)parsed;
	return 0;
}

// Parse a finite floating-point command line value.
static int parse_double(const char *text, const char *option, double *value)
{
	char *end;
	double parsed;

	errno = 0;
	parsed = strtod(text, &end);
	if (errno != 0 || end == text || *end != '\0' || !isfinite(parsed)) {
		fprintf(stderr, "Invalid value for -%s: %s\n", option, text);
		return -1;
	}
	*value = parsed;
	return 0;
}

// Wait for any one child, retrying if a signal interrupts waitpid.
static int reap_one_child(int *active_children)
{
	int status;
	pid_t child;

	do {
		child = waitpid(-1, &status, 0);
	} while (child == -1 && errno == EINTR);

	if (child == -1) {
		perror("waitpid");
		return -1;
	}

	--(*active_children);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		if (WIFEXITED(status)) {
			fprintf(stderr, "mandel child %ld exited with status %d\n",
				(long)child, WEXITSTATUS(status));
		} else {
			fprintf(stderr, "mandel child %ld ended abnormally\n", (long)child);
		}
		return 1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int process_limit = 0;
	int frame_count = DEFAULT_FRAME_COUNT;
	int max_iterations = DEFAULT_MAX_ITERATIONS;
	int width = DEFAULT_WIDTH;
	int height = DEFAULT_HEIGHT;
	double center_x = DEFAULT_CENTER_X;
	double center_y = DEFAULT_CENTER_Y;
	double initial_scale = DEFAULT_INITIAL_SCALE;
	double zoom_factor = DEFAULT_ZOOM_FACTOR;
	int option;

	while ((option = getopt(argc, argv, "p:n:x:y:s:z:m:W:H:h")) != -1) {
		switch (option) {
		case 'p':
			if (parse_integer(optarg, "p", &process_limit) != 0) return EXIT_FAILURE;
			break;
		case 'n':
			if (parse_integer(optarg, "n", &frame_count) != 0) return EXIT_FAILURE;
			break;
		case 'x':
			if (parse_double(optarg, "x", &center_x) != 0) return EXIT_FAILURE;
			break;
		case 'y':
			if (parse_double(optarg, "y", &center_y) != 0) return EXIT_FAILURE;
			break;
		case 's':
			if (parse_double(optarg, "s", &initial_scale) != 0 || initial_scale <= 0.0) {
				fprintf(stderr, "-s must be greater than zero\n");
				return EXIT_FAILURE;
			}
			break;
		case 'z':
			if (parse_double(optarg, "z", &zoom_factor) != 0 ||
				zoom_factor <= 0.0 || zoom_factor >= 1.0) {
				fprintf(stderr, "-z must be greater than zero and less than one\n");
				return EXIT_FAILURE;
			}
			break;
		case 'm':
			if (parse_integer(optarg, "m", &max_iterations) != 0) return EXIT_FAILURE;
			break;
		case 'W':
			if (parse_integer(optarg, "W", &width) != 0) return EXIT_FAILURE;
			break;
		case 'H':
			if (parse_integer(optarg, "H", &height) != 0) return EXIT_FAILURE;
			break;
		case 'h':
			show_help(stdout, argv[0]);
			return EXIT_SUCCESS;
		case '?':
		default:
			show_help(stderr, argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (optind != argc) {
		fprintf(stderr, "Unexpected argument: %s\n", argv[optind]);
		show_help(stderr, argv[0]);
		return EXIT_FAILURE;
	}
	if (process_limit == 0) {
		fprintf(stderr, "Specify the child process limit with -p.\n");
		show_help(stderr, argv[0]);
		return EXIT_FAILURE;
	}
	if (process_limit > frame_count) {
		fprintf(stderr, "-p cannot exceed the number of frames (-n).\n");
		return EXIT_FAILURE;
	}

	int next_frame = 0;
	int active_children = 0;
	int failed = 0;
	int fork_failed = 0;
	double scale = initial_scale;

	// Fill each available process slot, then wait for a child to finish before
	// launching more work. Each frame writes to a distinct JPEG file.
	while (active_children > 0 || (next_frame < frame_count && !fork_failed)) {
		while (next_frame < frame_count && active_children < process_limit && !fork_failed) {
			char x_text[64];
			char y_text[64];
			char scale_text[64];
			char width_text[32];
			char height_text[32];
			char iterations_text[32];
			char output_file[64];
			pid_t child;

			(void)snprintf(x_text, sizeof(x_text), "%.15g", center_x);
			(void)snprintf(y_text, sizeof(y_text), "%.15g", center_y);
			(void)snprintf(scale_text, sizeof(scale_text), "%.15g", scale);
			(void)snprintf(width_text, sizeof(width_text), "%d", width);
			(void)snprintf(height_text, sizeof(height_text), "%d", height);
			(void)snprintf(iterations_text, sizeof(iterations_text), "%d", max_iterations);
			(void)snprintf(output_file, sizeof(output_file), "mandel%d.jpg", next_frame);

			child = fork();
			if (child == -1) {
				perror("fork");
				failed = 1;
				fork_failed = 1;
				break;
			}
			if (child == 0) {
				// Replace this child with the existing single-image renderer.
				execl("./mandel", "mandel",
					"-x", x_text, "-y", y_text, "-s", scale_text,
					"-W", width_text, "-H", height_text,
					"-m", iterations_text, "-o", output_file,
					(char *)NULL);
				perror("exec ./mandel");
				_exit(127);
			}

			++active_children;
			++next_frame;
			// Zoom in progressively by shrinking the next frame's scale.
			scale *= zoom_factor;
		}

		if (active_children > 0) {
			// waitpid(-1, ...) reaps whichever child finishes first.
			int result = reap_one_child(&active_children);
			if (result != 0) failed = 1;
			if (result < 0) break;
		}
	}

	while (active_children > 0) {
		int result = reap_one_child(&active_children);
		if (result != 0) failed = 1;
		if (result < 0) break;
	}

	if (failed) {
		fprintf(stderr, "mandelmovie: one or more frames failed\n");
		return EXIT_FAILURE;
	}

	printf("Generated %d frames with a maximum of %d child processes.\n",
		 next_frame, process_limit);
	return EXIT_SUCCESS;
}
