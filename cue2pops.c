#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <libgen.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>

const int SECTORSIZE = 2352; 
#define VCD_HEADER_SIZE 0x100000
#define IO_BUFFER_SIZE  0x40000

int pregap_count = 0; 
int postgap_count = 0; 

static char g_tmp_vcd_path[1024] = {0};

typedef struct {
	int vmode;
	int trainer;
	int gap_more;
	int gap_less;
	int verbose;
	int debug_cue;

	int deny_vmode; 	
	int fix_game;		
	int game_has_cheats;	
	int game_title;
	int game_trained;
	int game_fixed;
} parameters;

typedef struct {
	const char *sig;
	size_t len;
	const char *name;
	int title_id;
	int deny_vmode;
} GameSignature;

static const GameSignature GAME_SIGNATURES[] = {
	{"SCES-00344", 10, "Crash Bandicoot [SCES-00344]", 1, 1},
	{"SCUS-94900", 10, "Crash Bandicoot [SCUS-94900]", 2, 0},
	{"SCPS-10031", 10, "Crash Bandicoot [SCPS-10031]", 3, 0}
};
static const size_t NUM_SIGNATURES = sizeof(GAME_SIGNATURES) / sizeof(GAME_SIGNATURES[0]);

static void cleanup_tmp_file(void) {
	if (g_tmp_vcd_path[0] != '\0') {
		unlink(g_tmp_vcd_path);
		g_tmp_vcd_path[0] = '\0';
	}
}

static void handle_signal(int sig) {
	(void)sig;
	cleanup_tmp_file();
	_exit(130);
}

static void setup_signals(void) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

static void log_error(const char *msg, const char *detail) {
	if (detail && strlen(detail) > 0) {
		fprintf(stderr, "[ERROR] %s: %s (errno=%d: %s)\n", msg, detail, errno, strerror(errno));
	} else {
		fprintf(stderr, "[ERROR] %s (errno=%d: %s)\n", msg, errno, strerror(errno));
	}
}

static void log_info(const parameters *p, const char *msg) {
	if (p->verbose || p->debug_cue) {
		printf("[INFO] %s\n", msg);
	}
}

static int check_disk_space(const char *path, int64_t required_bytes) {
	struct statvfs stat;
	char dir_buffer[1024];
	strncpy(dir_buffer, path, sizeof(dir_buffer) - 1);
	dir_buffer[sizeof(dir_buffer) - 1] = '\0';

	char *dir_name = dirname(dir_buffer);
	if (statvfs(dir_name, &stat) != 0) {
		return 0; 
	}

	uint64_t available_bytes = (uint64_t)stat.f_bavail * stat.f_frsize;
	if (available_bytes < (uint64_t)required_bytes) {
		fprintf(stderr, "[ERROR] Espaço em disco insuficiente. Necessário: %" PRId64 " bytes, Disponível: %" PRIu64 " bytes\n", required_bytes, available_bytes);
		return -1;
	}
	return 0;
}

static int create_dir_recursive(const char *dir_path) {
	char tmp[1024];
	char *p = NULL;
	size_t len;

	if (snprintf(tmp, sizeof(tmp), "%s", dir_path) >= (int)sizeof(tmp)) {
		fprintf(stderr, "[ERROR] Caminho de diretório muito longo\n");
		return -1;
	}

	len = strlen(tmp);
	if (len == 0) return 0;
	if (tmp[len - 1] == '/') tmp[len - 1] = 0;

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
				return -1;
			}
			*p = '/';
		}
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
		return -1;
	}
	return 0;
}

void game_identifier(unsigned char *inbuf, parameters *p)
{
	int ptr;

	if (p->debug_cue) {
		if (p->vmode == 0) {
			printf("----------------------------------------------------------------------------------\n");
		}
		printf("Hello from game_identifier!\n");
	}

	if (p->game_title == 0) {
		for (ptr = 0; ptr <= IO_BUFFER_SIZE - 16; ptr += 4) {
			for (size_t s = 0; s < NUM_SIGNATURES; s++) {
				if (memcmp(&inbuf[ptr], GAME_SIGNATURES[s].sig, GAME_SIGNATURES[s].len) == 0) {
					printf("----------------------------------------------------------------------------------\n");
					printf("%s\n", GAME_SIGNATURES[s].name);
					if (GAME_SIGNATURES[s].deny_vmode) p->deny_vmode++;
					p->game_title = GAME_SIGNATURES[s].title_id;
					p->game_has_cheats = 1;
					p->fix_game = 0;
					goto found_title;
				}
			}
		}
	}

found_title:
	if (p->game_title != 0 && p->fix_game == 1) {
		printf("GameFixer is ON\n");
	}
	if (p->game_title != 0 && p->trainer == 1 && p->game_has_cheats == 0) {
		printf("There is no cheat for this title\n");
	}
	if (p->game_title != 0 && p->deny_vmode != 0 && p->vmode == 1) {
		printf("VMODE patching is disabled for this title\n");
	}
}

void game_fixer(unsigned char *inbuf, parameters *p)
{
	int ptr;
	if (p->game_fixed == 0) {
		for (ptr = 0; ptr <= IO_BUFFER_SIZE - 8; ptr += 4) {
			if (p->game_title == 4) {
				if (inbuf[ptr] == 0x78 && inbuf[ptr+1] == 0x26 && inbuf[ptr+2] == 0x43 && inbuf[ptr+3] == 0x8C) inbuf[ptr] = 0x74;
				if (inbuf[ptr] == 0xE8 && inbuf[ptr+1] == 0x75 && inbuf[ptr+2] == 0x06 && inbuf[ptr+3] == 0x80) {
					if (ptr >= 8) {
						inbuf[ptr-8] = 0x07;
						printf("game_fixer : Disc Swap Patched\n");
						p->game_fixed = 1;
						break;
					}
				}
			}
		}
	}
}

void game_trainer(unsigned char *inbuf, parameters *p)
{
	int ptr;
	if (p->game_trained == 0) {
		for (ptr = 0; ptr <= IO_BUFFER_SIZE - 4; ptr += 4) {
			if (p->game_title == 1 && inbuf[ptr] == 0x7C && inbuf[ptr+1] == 0x16 && inbuf[ptr+2] == 0x20 && inbuf[ptr+3] == 0xAC) {
				inbuf[ptr+2] = 0x22;
				printf("game_trainer : Test Save System Enabled\n");
				p->game_trained = 1;
				break;
			}
			if (p->game_title == 2 && inbuf[ptr] == 0x9C && inbuf[ptr+1] == 0x19 && inbuf[ptr+2] == 0x20 && inbuf[ptr+3] == 0xAC) {
				inbuf[ptr+2] = 0x22;
				printf("game_trainer : Test Save System Enabled\n");
				p->game_trained = 1;
				break;
			}
			if (p->game_title == 3 && inbuf[ptr] == 0x84 && inbuf[ptr+1] == 0x19 && inbuf[ptr+2] == 0x20 && inbuf[ptr+3] == 0xAC) {
				inbuf[ptr+2] = 0x22;
				printf("game_trainer : Test Save System Enabled\n");
				p->game_trained = 1;
				break;
			}
		}
	}
}

int64_t get_file_size(const char *file_name)
{
	FILE *file_handle = fopen(file_name, "rb");
	if (!file_handle) {
		log_error("Cannot open file to get size", file_name);
		return -1;
	}

	if (fseek(file_handle, 0, SEEK_END) != 0) {
		log_error("Failed to seek file", file_name);
		fclose(file_handle);
		return -1;
	}

	int64_t size = ftell(file_handle);
	if (size < 0) {
		log_error("Failed to get file size", file_name);
		fclose(file_handle);
		return -1;
	}
	fclose(file_handle);
	return size;
}

int evaluate_arg(const char *arg, parameters *p)
{
	if (!strcmp(arg, "gap++")) { p->gap_more = 1; return 1; }
	if (!strcmp(arg, "gap--")) { p->gap_less = 1; return 1; }
	if (!strcmp(arg, "vmode")) { p->vmode = 1; return 1; }
	if (!strcmp(arg, "trainer")) { p->trainer = 1; return 1; }
	if (!strcmp(arg, "--verbose")) { p->verbose = 1; return 1; }
	if (!strcmp(arg, "--debug-cue")) { p->debug_cue = 1; p->verbose = 1; return 1; }
	if (!strcmp(arg, "--output") || !strcmp(arg, "-o")) {
		return 2; 
	}
	return 0;
}

int is_cue(const char *file_name)
{
	if (!file_name) return 0;
	FILE *file_handle = fopen(file_name, "rb");
	if (file_handle) {
		fclose(file_handle);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	FILE *bin_file = NULL;
	char *bin_path = NULL;
	int64_t bin_size = 0;

	FILE *cue_file = NULL;
	char *cue_name = NULL;
	char *cue_buf = NULL;
	char *cue_ptr = NULL;
	int64_t cue_size = 0;

	FILE *vcd_file = NULL;
	char *vcd_name = NULL;
	char *out_dir = NULL;

	int index1_count = 0;
	unsigned char *headerbuf = NULL;
	unsigned char *outbuf = NULL;
	int i;
	int track_count = 0;

	parameters params;
	memset(&params, 0, sizeof(params));

	setup_signals();

	printf("\nBIN/CUE to IMAGE0.VCD conversion tool v2.0 (Android/Termux Fixed)\n");

	if (argc <= 1) {
		printf("Usage: %s [options] <input.cue> [output.vcd]\n", argv[0]);
		printf("Options:\n");
		printf("  --output <dir> : Custom output directory\n");
		printf("  --verbose      : Verbose output mode\n");
		printf("  --debug-cue    : Detailed CUE/BIN path debugging\n");
		return 1;
	}

	for (i = 1; i < argc; i++) {
		int eval = evaluate_arg(argv[i], &params);
		if (eval == 2) {
			if (i + 1 < argc) {
				out_dir = strdup(argv[++i]);
			} else {
				fprintf(stderr, "[ERROR] Missing argument for --output\n");
				return 1;
			}
		} else if (eval == 0) {
			if (cue_name == NULL) {
				cue_name = strdup(argv[i]);
			} else if (vcd_name == NULL) {
				vcd_name = strdup(argv[i]);
			}
		}
	}

	if (!cue_name) {
		fprintf(stderr, "[ERROR] No input CUE file specified\n");
		if (out_dir) free(out_dir);
		return 1;
	}

	if (!is_cue(cue_name)) {
		log_error("Cannot open or access CUE file", cue_name);
		fprintf(stderr, "[FAIL] %s\n", cue_name);
		if (out_dir) free(out_dir);
		free(cue_name);
		if (vcd_name) free(vcd_name);
		return 1;
	}

	char cue_dir[1024] = {0};
	if (strlen(cue_name) >= sizeof(cue_dir)) {
		fprintf(stderr, "[ERROR] Input path exceeds buffer limits\n");
		if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}
	strncpy(cue_dir, cue_name, sizeof(cue_dir) - 1);
	char *dir_part = dirname(cue_dir);

	if (params.debug_cue) {
		printf("[DEBUG] CUE Path: %s\n", cue_name);
		printf("[DEBUG] CUE Directory: %s\n", dir_part);
	}

	cue_size = get_file_size(cue_name);
	if (cue_size <= 0) {
		log_error("Invalid or empty CUE file", cue_name);
		fprintf(stderr, "[FAIL] %s\n", cue_name);
		if (out_dir) free(out_dir);
		free(cue_name);
		if (vcd_name) free(vcd_name);
		return 1;
	}

	cue_file = fopen(cue_name, "rb");
	if (!cue_file) {
		log_error("Failed opening CUE file", cue_name);
		fprintf(stderr, "[FAIL] %s\n", cue_name);
		if (out_dir) free(out_dir);
		free(cue_name);
		if (vcd_name) free(vcd_name);
		return 1;
	}

	cue_buf = calloc(1, cue_size + 1);
	if (!cue_buf) {
		log_error("Memory allocation failed for CUE buffer", NULL);
		fclose(cue_file);
		if (out_dir) free(out_dir);
		free(cue_name);
		if (vcd_name) free(vcd_name);
		return 1;
	}

	if (fread(cue_buf, 1, cue_size, cue_file) != (size_t)cue_size) {
		log_error("Failed reading CUE buffer", cue_name);
		free(cue_buf);
		fclose(cue_file);
		if (out_dir) free(out_dir);
		free(cue_name);
		if (vcd_name) free(vcd_name);
		return 1;
	}
	fclose(cue_file);

	cue_ptr = NULL;
	for (i = 0; i < cue_size - 4; i++) {
		if (strncasecmp(&cue_buf[i], "FILE", 4) == 0) {
			if (i == 0 || isspace((unsigned char)cue_buf[i - 1])) {
				cue_ptr = &cue_buf[i];
				break;
			}
		}
	}

	if (!cue_ptr) {
		fprintf(stderr, "[ERROR] Invalid CUE format: FILE directive not found\n");
		free(cue_buf);
		fprintf(stderr, "[FAIL] %s\n", cue_name);
		if (out_dir) free(out_dir);
		free(cue_name);
		if (vcd_name) free(vcd_name);
		return 1;
	}

	cue_ptr += 4;
	while (*cue_ptr == ' ' || *cue_ptr == '\t') cue_ptr++;

	char extracted_bin[512] = {0};
	if (*cue_ptr == '"') {
		cue_ptr++;
		char *end_quote = strchr(cue_ptr, '"');
		if (!end_quote) {
			fprintf(stderr, "[ERROR] Invalid CUE format: Closing quote for FILE missing\n");
			free(cue_buf);
			if (out_dir) free(out_dir);
			free(cue_name);
			if (vcd_name) free(vcd_name);
			return 1;
		}
		if ((size_t)(end_quote - cue_ptr) >= sizeof(extracted_bin)) {
			fprintf(stderr, "[ERROR] BIN filename in CUE is too long\n");
			free(cue_buf); if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
			return 1;
		}
		strncpy(extracted_bin, cue_ptr, end_quote - cue_ptr);
	} else {
		char *end_space = strpbrk(cue_ptr, " \t\r\n");
		size_t len = end_space ? (size_t)(end_space - cue_ptr) : strlen(cue_ptr);
		if (len >= sizeof(extracted_bin)) {
			fprintf(stderr, "[ERROR] BIN filename in CUE is too long\n");
			free(cue_buf); if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
			return 1;
		}
		strncpy(extracted_bin, cue_ptr, len);
	}

	if (params.debug_cue) {
		printf("[DEBUG] FILE directive extracted: %s\n", extracted_bin);
	}

	bin_path = malloc(1024);
	if (!bin_path) {
		log_error("Memory allocation failed for BIN path", NULL);
		free(cue_buf);
		if (out_dir) free(out_dir);
		free(cue_name);
		if (vcd_name) free(vcd_name);
		return 1;
	}

	if (extracted_bin[0] == '/' || (strlen(extracted_bin) > 2 && extracted_bin[1] == ':')) {
		if (snprintf(bin_path, 1024, "%s", extracted_bin) >= 1024) {
			fprintf(stderr, "[ERROR] BIN path exceeds limits\n");
			free(cue_buf); free(bin_path); if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
			return 1;
		}
	} else {
		if (snprintf(bin_path, 1024, "%s/%s", dir_part, extracted_bin) >= 1024) {
			fprintf(stderr, "[ERROR] BIN path exceeds limits\n");
			free(cue_buf); free(bin_path); if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
			return 1;
		}
	}

	if (params.debug_cue) {
		printf("[DEBUG] Resolved BIN Path: %s\n", bin_path);
	}

	bin_size = get_file_size(bin_path);
	if (bin_size <= 0) {
		log_error("Cannot locate or read associated BIN file", bin_path);
		free(cue_buf);
		free(bin_path);
		fprintf(stderr, "[FAIL] %s\n", cue_name);
		if (out_dir) free(out_dir);
		free(cue_name);
		if (vcd_name) free(vcd_name);
		return 1;
	}

	if (params.verbose) {
		printf("[INFO] Opening BIN file: %s (Size: %" PRId64 " bytes)\n", bin_path, bin_size);
	}

	char base_vcd_name[256] = {0};
	char *tmp_cue_dup = strdup(cue_name);
	char *cue_filename = basename(tmp_cue_dup);
	strncpy(base_vcd_name, cue_filename, sizeof(base_vcd_name) - 1);
	free(tmp_cue_dup);

	char *dot = strrchr(base_vcd_name, '.');
	if (dot) *dot = '\0';
	if (strlen(base_vcd_name) + 4 < sizeof(base_vcd_name)) {
		strcat(base_vcd_name, ".VCD");
	} else {
		fprintf(stderr, "[ERROR] Target filename too long\n");
		free(cue_buf); free(bin_path); if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	char final_vcd_path[1024] = {0};

	if (out_dir) {
		if (create_dir_recursive(out_dir) != 0) {
			log_error("Cannot create output directory", out_dir);
			free(cue_buf);
			free(bin_path);
			free(out_dir);
			free(cue_name);
			if (vcd_name) free(vcd_name);
			return 1;
		}
		size_t len = strlen(out_dir);
		if (out_dir[len - 1] == '/') {
			if (snprintf(final_vcd_path, sizeof(final_vcd_path), "%s%s", out_dir, base_vcd_name) >= (int)sizeof(final_vcd_path)) {
				fprintf(stderr, "[ERROR] Output path too long\n");
				free(cue_buf); free(bin_path); free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
				return 1;
			}
		} else {
			if (snprintf(final_vcd_path, sizeof(final_vcd_path), "%s/%s", out_dir, base_vcd_name) >= (int)sizeof(final_vcd_path)) {
				fprintf(stderr, "[ERROR] Output path too long\n");
				free(cue_buf); free(bin_path); free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
				return 1;
			}
		}
	} else if (vcd_name) {
		if (snprintf(final_vcd_path, sizeof(final_vcd_path), "%s", vcd_name) >= (int)sizeof(final_vcd_path)) {
			fprintf(stderr, "[ERROR] Output path too long\n");
			free(cue_buf); free(bin_path); free(cue_name); free(vcd_name);
			return 1;
		}
	} else {
		if (snprintf(final_vcd_path, sizeof(final_vcd_path), "%s/%s", dir_part, base_vcd_name) >= (int)sizeof(final_vcd_path)) {
			fprintf(stderr, "[ERROR] Output path too long\n");
			free(cue_buf); free(bin_path); free(cue_name);
			return 1;
		}
	}

	if (snprintf(g_tmp_vcd_path, sizeof(g_tmp_vcd_path), "%s.tmp", final_vcd_path) >= (int)sizeof(g_tmp_vcd_path)) {
		fprintf(stderr, "[ERROR] Temporary path exceeds limits\n");
		free(cue_buf); free(bin_path); if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	if (check_disk_space(g_tmp_vcd_path, bin_size + VCD_HEADER_SIZE) != 0) {
		free(cue_buf); free(bin_path); if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	if (params.verbose) {
		printf("[INFO] Output VCD path: %s\n", final_vcd_path);
	}

	headerbuf = calloc(1, VCD_HEADER_SIZE);
	if (!headerbuf) {
		log_error("Failed to allocate header buffer", NULL);
		free(cue_buf);
		free(bin_path);
		if (out_dir) free(out_dir);
		free(cue_name);
		if (vcd_name) free(vcd_name);
		return 1;
	}

	headerbuf[0] = 0x41;
	headerbuf[2] = 0xA0;
	headerbuf[7] = 0x01;
	headerbuf[8] = 0x20;
	headerbuf[12] = 0xA1;
	headerbuf[22] = 0xA2;

	for (i = 0; i < cue_size - 5; i++) {
		if (!strncasecmp(&cue_buf[i], "TRACK", 5)) track_count++;
		if (!strncasecmp(&cue_buf[i], "INDEX 01", 8)) index1_count++;
		if (!strncasecmp(&cue_buf[i], "PREGAP", 6)) pregap_count++;
		if (!strncasecmp(&cue_buf[i], "POSTGAP", 7)) postgap_count++;
	}

	if (track_count == 0 || track_count != index1_count) {
		fprintf(stderr, "[ERROR] Invalid CUE structure: Track count mismatch\n");
		free(cue_buf); free(bin_path); free(headerbuf);
		if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	bin_file = fopen(bin_path, "rb");
	if (!bin_file) {
		log_error("Cannot open BIN file for processing", bin_path);
		free(cue_buf); free(bin_path); free(headerbuf);
		if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	vcd_file = fopen(g_tmp_vcd_path, "wb");
	if (!vcd_file) {
		log_error("Cannot create temporary VCD output file", g_tmp_vcd_path);
		fclose(bin_file); free(cue_buf); free(bin_path); free(headerbuf);
		if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	outbuf = malloc(IO_BUFFER_SIZE);
	if (!outbuf) {
		log_error("Cannot allocate conversion buffer", NULL);
		fclose(bin_file); fclose(vcd_file); cleanup_tmp_file(); free(cue_buf); free(bin_path); free(headerbuf);
		if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	uint64_t total_sectors = ((uint64_t)bin_size / SECTORSIZE) + (150 * (pregap_count + postgap_count));
	headerbuf[1032] = (unsigned char)(total_sectors & 0xFF);
	headerbuf[1033] = (unsigned char)((total_sectors >> 8) & 0xFF);
	headerbuf[1034] = (unsigned char)((total_sectors >> 16) & 0xFF);

	headerbuf[1036] = (unsigned char)(total_sectors & 0xFF);
	headerbuf[1037] = (unsigned char)((total_sectors >> 8) & 0xFF);
	headerbuf[1038] = (unsigned char)((total_sectors >> 16) & 0xFF);

	if (fwrite(headerbuf, 1, VCD_HEADER_SIZE, vcd_file) != VCD_HEADER_SIZE) {
		log_error("Failed writing VCD header", g_tmp_vcd_path);
		fclose(bin_file); fclose(vcd_file); cleanup_tmp_file();
		free(cue_buf); free(bin_path); free(headerbuf); free(outbuf);
		if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	log_info(&params, "Writing BIN sectors to VCD...");

	size_t bytes_read = fread(outbuf, 1, IO_BUFFER_SIZE, bin_file);
	if (bytes_read > 0) {
		game_identifier(outbuf, &params);
		game_fixer(outbuf, &params);
		game_trainer(outbuf, &params);

		if (fwrite(outbuf, 1, bytes_read, vcd_file) != bytes_read) {
			log_error("Writing failure during initial block creation", g_tmp_vcd_path);
			fclose(bin_file); fclose(vcd_file); cleanup_tmp_file();
			free(cue_buf); free(bin_path); free(headerbuf); free(outbuf);
			if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
			return 1;
		}
	} else if (ferror(bin_file)) {
		log_error("Read error on initial BIN block", bin_path);
		fclose(bin_file); fclose(vcd_file); cleanup_tmp_file();
		free(cue_buf); free(bin_path); free(headerbuf); free(outbuf);
		if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	while ((bytes_read = fread(outbuf, 1, IO_BUFFER_SIZE, bin_file)) > 0) {
		if (fwrite(outbuf, 1, bytes_read, vcd_file) != bytes_read) {
			log_error("Writing failure during VCD creation", g_tmp_vcd_path);
			fclose(bin_file); fclose(vcd_file); cleanup_tmp_file();
			free(cue_buf); free(bin_path); free(headerbuf); free(outbuf);
			if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
			return 1;
		}
	}

	if (ferror(bin_file)) {
		log_error("Read failure from BIN file", bin_path);
		fclose(bin_file); fclose(vcd_file); cleanup_tmp_file();
		free(cue_buf); free(bin_path); free(headerbuf); free(outbuf);
		if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	fflush(vcd_file);
	int vcd_fd = fileno(vcd_file);
	if (vcd_fd != -1) {
		fsync(vcd_fd);
	}

	fclose(bin_file);
	if (fclose(vcd_file) != 0) {
		log_error("Failed closing output file cleanly", g_tmp_vcd_path);
		cleanup_tmp_file();
		free(cue_buf); free(bin_path); free(headerbuf); free(outbuf);
		if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	if (rename(g_tmp_vcd_path, final_vcd_path) != 0) {
		log_error("Failed moving temporary VCD to final target", final_vcd_path);
		cleanup_tmp_file();
		free(cue_buf); free(bin_path); free(headerbuf); free(outbuf);
		if (out_dir) free(out_dir); free(cue_name); if (vcd_name) free(vcd_name);
		return 1;
	}

	g_tmp_vcd_path[0] = '\0';

	free(cue_buf);
	free(bin_path);
	free(headerbuf);
	free(outbuf);
	if (out_dir) free(out_dir);
	free(cue_name);
	if (vcd_name) free(vcd_name);

	printf("[OK] Converted: %s\n", final_vcd_path);
	return 0;
}
