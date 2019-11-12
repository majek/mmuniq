#include <byteswap.h>
#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "csiphash.c"

/* Input buffer size. Limit of line length. */
#define BUF_SZ (1024 * 1024)

/* Stdout buffer when piping to not-tty. */
#define STDOUTBUF_SZ (1024 * 1024)

#define DEFAULT_BLOOM_N 500000000
#define DEFAULT_BLOOM_P 0.0001
#define DEFAULT_BLOOM_K 8

static void print_usage(void)
{
	printf("Usage: ./mmuniq < input-file > output-file\n"
	       "Filters duplicate lines using probabilistic bloom filter.\n"
	       "Reads newline-delimited data from STDIN, writes unique lines\n"
	       "to STDOUT.\n"
	       "\n"
	       "  -n   Estimated cardinality of input. Default %d\n"
	       "  -p   Desired false positive probability. Default %f\n"
	       "  -k   Number of hash functions to use. Default %d\n"
	       "  -q   Quiet.\n"
	       "  -D   Print all repeated lines.\n"
	       "",
	       DEFAULT_BLOOM_N, DEFAULT_BLOOM_P, DEFAULT_BLOOM_K);
}

static int bitmap_getset(uint64_t *bm, uint64_t n)
{
	uint64_t mask = 0x1ULL << (n % 64);
	uint64_t *p = &bm[n / 64];
	uint64_t v = *p;
	uint64_t r = v & mask;
	if (r == 0) {
		*p = v | mask;
		return 0;
	} else {
		return 1;
	}
}

const char *optstring_from_long_options(const struct option *opt)
{
	static char optstring[256] = {0};
	char *osp = optstring;

	for (; opt->name != NULL; opt++) {
		if (opt->flag == 0 && opt->val > 0 && opt->val < 256) {
			*osp++ = opt->val;
			switch (opt->has_arg) {
			case optional_argument:
				*osp++ = ':';
				*osp++ = ':';
				break;
			case required_argument:
				*osp++ = ':';
				break;
			}
		}
	}
	*osp++ = '\0';

	if (osp - optstring >= (int)sizeof(optstring)) {
		abort();
	}
	return optstring;
}

static uint64_t global_bloom_m;
static int global_bloom_k = DEFAULT_BLOOM_K;
static int global_repeated;
static int surpress_output;

static const uint64_t k0 = 0xdeadbeeffadebabeULL;
static const uint64_t k1 = 0xdeadbeeffadebabeULL;

static void process_line(char *s, int l, uint64_t *bm)
{

	int miss = 0;
	int i;
	for (i = 0; i < global_bloom_k; i += 8) {
		uint64_t res[8];
		uint64_t off = i * 0x1000100010001000;
		siphash24((const uint8_t *)s, l, k0 + off, k1 + off, res);

		if (i+4 < global_bloom_k) {
			res[4] = bswap_64(res[0]) % global_bloom_m;
			res[5] = bswap_64(res[1]) % global_bloom_m;
			res[6] = bswap_64(res[2]) % global_bloom_m;
			res[7] = bswap_64(res[3]) % global_bloom_m;
		}
		res[0] %= global_bloom_m;
		res[1] %= global_bloom_m;
		res[2] %= global_bloom_m;
		res[3] %= global_bloom_m;

		int j;
		for (j = 0; j < 8; j++) {
			__builtin_prefetch(&bm[res[j] / 64]);
		}

		for (j = i; j < i + 8 && j < global_bloom_k; j++) {
			uint64_t hash = res[j % 8];
			miss |= bitmap_getset(bm, hash) == 0;
		}
	}

	if ((global_repeated == 0 && miss == 1) ||
	    (global_repeated == 1 && miss == 0)) {
		if (surpress_output == 0) {
			fwrite(s, l, 1, stdout);
			fputc('\n', stdout);
		}
	}
}

int main(int argc, char **argv)
{
	uint64_t bloom_n = DEFAULT_BLOOM_N;
	double bloom_p = DEFAULT_BLOOM_P;
	int quiet = 0;

	uint64_t forced_bloom_m = 0;

	{
		static struct option long_options[] = {
			{"n", required_argument, 0, 'n'},
			{"p", required_argument, 0, 'p'},
			{"k", required_argument, 0, 'k'},
			{"q", no_argument, 0, 'q'},
			{"all-repeated", no_argument, 0, 'D'},
			{"help", no_argument, 0, 'h'},
			{"force-m", required_argument, 0, 'm'},
			{NULL, 0, 0, 0}};
		optind = 1;
		while (1) {
			int option_index = 0;
			int arg = getopt_long(
				argc, argv,
				optstring_from_long_options(long_options),
				long_options, &option_index);
			if (arg == -1) {
				break;
			}

			switch (arg) {
			default:
			case 0:
				fprintf(stderr, "Unknown option: %s",
					argv[optind]);
				exit(-1);
				break;
			case '?':
				exit(-1);
				break;
			case 'h':
				print_usage();
				exit(1);
				break;
			case 'n':
				bloom_n = llabs(atoll(optarg));
				break;
			case 'p':
				bloom_p = fabs(atof(optarg));
				break;
			case 'k':
				global_bloom_k = abs(atoi(optarg));
				break;
			case 'q':
				quiet = 1;
				break;
			case 'm':
				forced_bloom_m = atoll(optarg);
				surpress_output = 1;
				break;
			case 'D':
				global_repeated = 1;
				break;
			}
		}
	}


	if (forced_bloom_m == 0) {
		double bloom_r = -(double)global_bloom_k /
			log(1. - exp(log(bloom_p) / (double)global_bloom_k));

		global_bloom_m = ceil((double)bloom_n * bloom_r);
	} else {
		global_bloom_m = forced_bloom_m;
	}

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))
	size_t bm_sz = DIV_ROUND_UP(global_bloom_m, 64) * 64 / 8;

	uint64_t *bm = mmap(NULL, bm_sz, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);

	if (bm == MAP_FAILED) {
		error(-1, errno, "mmap()");
	}

	if (isatty(1) == 0) {
		char *stdoutbuff = calloc(1, STDOUTBUF_SZ);
		fflush(stdout);
		setvbuf(stdout, stdoutbuff, _IOFBF, STDOUTBUF_SZ);
	}

	if (quiet == 0) {
		if (forced_bloom_m == 0) {
			fprintf(stderr, "[.] Bloom parameters: ");
			fprintf(stderr, "n=%lu ", bloom_n);
			fprintf(stderr, "p=%f ", bloom_p);
			fprintf(stderr, "k=%i ", global_bloom_k);
			fprintf(stderr, "m=%lu MiB\n", global_bloom_m / 8 / 1024 / 1024);
		} else {
			fprintf(stderr, "[.] Not producing output. Forced bloom parameters: ");
			fprintf(stderr, "k=%i ", global_bloom_k);
			fprintf(stderr, "m=%lu MiB\n", global_bloom_m / 8 / 1024 / 1024);
		}
	}

	char buf[BUF_SZ];
	int buf_pos = 0;
	while (1) {
		int r = read(0, &buf[buf_pos], BUF_SZ - buf_pos);
		if (r <= 0) {
			break;
		}

		int buf_sz = buf_pos + r;
		buf_pos = 0;
		while (1) {
			char *nl =
				memchr(&buf[buf_pos], '\n', buf_sz - buf_pos);
			if (nl == NULL) {
				break;
			}
			int nl_idx = nl - &buf[0];
			int sz = nl_idx - buf_pos;
			process_line(&buf[buf_pos], sz, bm);
			buf_pos = nl_idx + 1;
		}
		memmove(&buf[0], &buf[buf_pos], buf_sz - buf_pos);
		buf_pos = buf_sz - buf_pos;

		// Filled buffer, no new line in sight
		if (buf_pos == BUF_SZ) {
			process_line(&buf[0], BUF_SZ, bm);
			buf_pos = 0;
		}
	}
	fflush(stdout);
}
