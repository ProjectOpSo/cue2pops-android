/* 	BIN/CUE to IMAGE0.VCD conversion tool v2.0 (Android / Termux)
	Updated for C11 and strict path handling with full error diagnostics.
*/

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
#include <libgen.h>
#include <ctype.h>

const int SECTORSIZE = 2352; 
const int HEADERSIZE = 0x100000; 

int pregap_count = 0; 
int postgap_count = 0; 

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

static int create_dir_recursive(const char *dir_path) {
	char tmp[1024];
	char *p = NULL;
	size_t len;

	snprintf(tmp, sizeof(tmp), "%s", dir_path);
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
		for (ptr = 0; ptr < HEADERSIZE - 16; ptr += 4) {
			if (inbuf[ptr] == 'S' && inbuf[ptr+1] == 'C' && inbuf[ptr+2] == 'E' && inbuf[ptr+3] == 'S' && inbuf[ptr+4] == '-' && inbuf[ptr+5] == '0' && inbuf[ptr+6] == '0' && inbuf[ptr+7] == '3' && inbuf[ptr+8] == '4' && inbuf[ptr+9] == '4') {
				printf("----------------------------------------------------------------------------------\n");
				printf("Crash Bandicoot [SCES-00344]\n");
				p->deny_vmode++;
				p->game_title = 1;
				p->game_has_cheats = 1;
				p->fix_game = 0;
				break;
			}
			if (inbuf[ptr] == 'S' && inbuf[ptr+1] == 'C' && inbuf[ptr+2] == 'U' && inbuf[ptr+3] == 'S' && inbuf[ptr+4] == '-' && inbuf[ptr+5] == '9' && inbuf[ptr+6] == '4' && inbuf[ptr+7] == '9' && inbuf[ptr+8] == '0' && inbuf[ptr+9] == '0') {
				printf("----------------------------------------------------------------------------------\n");
				printf("Crash Bandicoot [SCUS-94900]\n");
				p->game_title = 2;
				p->game_has_cheats = 1;
				p->fix_game = 0;
				break;
			}
			if (inbuf[ptr] == 'S' && inbuf[ptr+1] == 'C' && inbuf[ptr+2] == 'P' && inbuf[ptr+3] == 'S' && inbuf[ptr+4] == '_' && inbuf[ptr+5] == '1' && inbuf[ptr+6] == '0' && inbuf[ptr+7] == '0' && inbuf[ptr+8] == '3' && inbuf[ptr+9] == '1') {
				printf("----------------------------------------------------------------------------------\n");
				printf("Crash Bandicoot [SCPS-10031]\n");
				p->game_title = 3;
				p->game_has_cheats = 1;
				p->fix_game = 0;
				break;
			}
		}
	}

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
		for (ptr = 0; ptr < HEADERSIZE - 8; ptr += 4) {
			if (p->game_title == 4) {
				if (inbuf[ptr] == 0x78 && inbuf[ptr+1] == 0x26 && inbuf[ptr+2] == 0x43 && inbuf[ptr+3] == 0x8C) inbuf[ptr] = 0x74;
				if (inbuf[ptr] == 0xE8 && inbuf[ptr+1] == 0x75 && inbuf[ptr+2] == 0x06 && inbuf[ptr+3] == 0x80) {
					inbuf[ptr-8] = 0x07;
					printf("game_fixer : Disc Swap Patched\n");
					p->game_fixed = 1;
					break;
				}
			}
		}
	}
}

void game_trainer(unsigned char *inbuf, parameters *p)
{
	int ptr;
	if (p->game_trained == 0) {
		for (ptr = 0; ptr < HEADERSIZE - 4; ptr += 4) {
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
	int sector_count;

	parameters params;
	memset(&params, 0, sizeof(params));

	printf("\nBIN/CUE to IMAGE0.VCD conversion tool v2.0 (Android Fixed)\n");

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
		return 1;
	}

	if (!is_cue(cue_name)) {
		log_error("Cannot open or access CUE file", cue_name);
		fprintf(stderr, "[FAIL] %s\n", cue_name);
		return 1;
	}

	char cue_dir[1024] = {0};
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
		return 1;
	}

	cue_file = fopen(cue_name, "rb");
	if (!cue_file) {
		log_error("Failed opening CUE file", cue_name);
		fprintf(stderr, "[FAIL] %s\n", cue_name);
		return 1;
	}

	cue_buf = calloc(1, cue_size + 1);
	if (!cue_buf) {
		log_error("Memory allocation failed for CUE buffer", NULL);
		fclose(cue_file);
		return 1;
	}

	if (fread(cue_buf, cue_size, 1, cue_file) != 1) {
		log_error("Failed reading CUE buffer", cue_name);
		free(cue_buf);
		fclose(cue_file);
		return 1;
	}
	fclose(cue_file);

	cue_ptr = strstr(cue_buf, "FILE ");
	if (!cue_ptr) {
		fprintf(stderr, "[ERROR] Invalid CUE format: FILE directive not found\n");
		free(cue_buf);
		fprintf(stderr, "[FAIL] %s\n", cue_name);
		return 1;
	}

	cue_ptr += 5;
	while (*cue_ptr == ' ' || *cue_ptr == '\t') cue_ptr++;

	char extracted_bin[512] = {0};
	if (*cue_ptr == '"') {
		cue_ptr++;
		char *end_quote = strchr(cue_ptr, '"');
		if (!end_quote) {
			fprintf(stderr, "[ERROR] Invalid CUE format: Closing quote for FILE missing\n");
			free(cue_buf);
			return 1;
		}
		strncpy(extracted_bin, cue_ptr, end_quote - cue_ptr);
	} else {
		char *end_space = strpbrk(cue_ptr, " \t\r\n");
		if (end_space) {
			strncpy(extracted_bin, cue_ptr, end_space - cue_ptr);
		} else {
			strcpy(extracted_bin, cue_ptr);
		}
	}

	if (params.debug_cue) {
		printf("[DEBUG] FILE directive extracted: %s\n", extracted_bin);
	}

	bin_path = malloc(1024);
	if (!bin_path) {
		log_error("Memory allocation failed for BIN path", NULL);
		free(cue_buf);
		return 1;
	}

	if (extracted_bin[0] == '/' || (strlen(extracted_bin) > 2 && extracted_bin[1] == ':')) {
		snprintf(bin_path, 1024, "%s", extracted_bin);
	} else {
		snprintf(bin_path, 1024, "%s/%s", dir_part, extracted_bin);
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
		return 1;
	}

	if (params.verbose) {
		printf("[INFO] Opening BIN file: %s (Size: %" PRId64 " bytes)\n", bin_path, bin_size);
	}

	char base_vcd_name[256] = {0};
	char *cue_filename = basename(strdup(cue_name));
	strncpy(base_vcd_name, cue_filename, sizeof(base_vcd_name) - 1);
	char *dot = strrchr(base_vcd_name, '.');
	if (dot) *dot = '\0';
	strcat(base_vcd_name, ".VCD");

	char final_vcd_path[1024] = {0};

	if (out_dir) {
		if (create_dir_recursive(out_dir) != 0) {
			log_error("Cannot create output directory", out_dir);
			free(cue_buf);
			free(bin_path);
			return 1;
		}
		size_t len = strlen(out_dir);
		if (out_dir[len - 1] == '/') {
			snprintf(final_vcd_path, sizeof(final_vcd_path), "%s%s", out_dir, base_vcd_name);
		} else {
			snprintf(final_vcd_path, sizeof(final_vcd_path), "%s/%s", out_dir, base_vcd_name);
		}
	} else if (vcd_name) {
		snprintf(final_vcd_path, sizeof(final_vcd_path), "%s", vcd_name);
	} else {
		snprintf(final_vcd_path, sizeof(final_vcd_path), "%s/%s", dir_part, base_vcd_name);
	}

	if (params.verbose) {
		printf("[INFO] Output VCD path: %s\n", final_vcd_path);
	}

	headerbuf = calloc(1, HEADERSIZE);
	if (!headerbuf) {
		log_error("Failed to allocate header buffer", NULL);
		free(cue_buf);
		free(bin_path);
		return 1;
	}

	headerbuf[0] = 0x41;
	headerbuf[2] = 0xA0;
	headerbuf[7] = 0x01;
	headerbuf[8] = 0x20;
	headerbuf[12] = 0xA1;
	headerbuf[22] = 0xA2;

	for (i = 0; i < cue_size - 6; i++) {
		if (!strncmp(&cue_buf[i], "TRACK ", 6)) track_count++;
		if (!strncmp(&cue_buf[i], "INDEX 01", 8)) index1_count++;
		if (!strncmp(&cue_buf[i], "PREGAP", 6)) pregap_count++;
		if (!strncmp(&cue_buf[i], "POSTGAP", 7)) postgap_count++;
	}

	if (track_count == 0 || track_count != index1_count) {
		fprintf(stderr, "[ERROR] Invalid CUE structure: Track count mismatch\n");
		free(cue_buf); free(bin_path); free(headerbuf);
		return 1;
	}

	bin_file = fopen(bin_path, "rb");
	if (!bin_file) {
		log_error("Cannot open BIN file for processing", bin_path);
		free(cue_buf); free(bin_path); free(headerbuf);
		return 1;
	}

	vcd_file = fopen(final_vcd_path, "wb");
	if (!vcd_file) {
		log_error("Cannot create VCD output file", final_vcd_path);
		fclose(bin_file); free(cue_buf); free(bin_path); free(headerbuf);
		return 1;
	}

	outbuf = malloc(HEADERSIZE);
	if (!outbuf) {
		log_error("Cannot allocate conversion buffer", NULL);
		fclose(bin_file); fclose(vcd_file); free(cue_buf); free(bin_path); free(headerbuf);
		return 1;
	}

	if (fread(outbuf, HEADERSIZE, 1, bin_file) == 1) {
		game_identifier(outbuf, &params);
		game_fixer(outbuf, &params);
		game_trainer(outbuf, &params);
	}
	fseek(bin_file, 0, SEEK_SET);

	sector_count = (bin_size / SECTORSIZE) + (150 * (pregap_count + postgap_count));
	memcpy(headerbuf + 1032, &sector_count, 3);
	memcpy(headerbuf + 1036, &sector_count, 3);

	fwrite(headerbuf, HEADERSIZE, 1, vcd_file);

	log_info(&params, "Writing BIN sectors to VCD...");

	size_t bytes_read;
	while ((bytes_read = fread(outbuf, 1, HEADERSIZE, bin_file)) > 0) {
		if (fwrite(outbuf, 1, bytes_read, vcd_file) != bytes_read) {
			log_error("Writing failure during VCD creation", final_vcd_path);
			fclose(bin_file); fclose(vcd_file);
			free(cue_buf); free(bin_path); free(headerbuf); free(outbuf);
			return 1;
		}
	}

	fclose(bin_file);
	fclose(vcd_file);
	free(cue_buf);
	free(bin_path);
	free(headerbuf);
	free(outbuf);

	printf("[OK] Converted: %s\n", final_vcd_path);
	return 0;
}
