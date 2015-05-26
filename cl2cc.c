/*	cl2cc
	Copyright 2015 libdll.so

	This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#include <windows.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#define VERSION "1.0"

static char *cc_command_line;
static unsigned int cc_command_line_length;
static unsigned int cc_command_line_max_length;

static void __attribute__((__noreturn__)) fatal(int e) {
	assert(e);
	MessageBoxA(NULL, strerror(e), NULL, MB_ICONHAND);
	exit(-e);
}

void init_command_line() {
	cc_command_line_max_length = PATH_MAX * 2 + 1; 
	cc_command_line = malloc(cc_command_line_max_length);
	if(!cc_command_line) fatal(ENOMEM);
	const char *cc = getenv("CC");
	if(!cc) cc = "cc";
	size_t len = strlen(cc);
	strcpy(cc_command_line, cc);
	cc_command_line_length = len;
}

static int have_space(const char *s) {
	while(*s) {
		if(*s == ' ' || *s == '	') return 1;
		s++;
	}
	return 0;
}

void add_arg(const char *arg) {
	size_t len = strlen(arg) + 1;
	//if(have_space(arg)) len += 2;
	int need_quote = have_space(arg);
	//if(need_quote) len += 2;
	if(cc_command_line_length + len + need_quote ? 2 : 0 > cc_command_line_max_length) {
		cc_command_line_max_length += PATH_MAX;
		cc_command_line = realloc(cc_command_line, cc_command_line_max_length);
		if(!cc_command_line) fatal(ENOMEM);
	}
	cc_command_line[cc_command_line_length] = ' ';
	if(need_quote) cc_command_line[++cc_command_line_length] = '\"';
	memcpy(cc_command_line + cc_command_line_length + 1, arg, len);
	cc_command_line_length += len;
	if(need_quote) {
		cc_command_line[cc_command_line_length++] = '\"';
		cc_command_line[cc_command_line_length] = 0;
	}
}

static int get_last_dot(const char *s, size_t len) {
	while(--len) if(s[len] == '.') break;
	if(!len) return -1;
	return len;
}

int start_cc() {
	const char *compiler = getenv("CC_LOCATION");
	STARTUPINFOA si = { .cb = sizeof(STARTUPINFOA) };
	PROCESS_INFORMATION pi;
	while(!CreateProcessA(compiler, cc_command_line, NULL, NULL, 0, 0, NULL, NULL, &si, &pi)) {
		if(compiler) {
			compiler = NULL;
			continue;
		}
		fprintf(stderr, "CreateProcessA failed, error %lu\n", GetLastError());
		exit(127);
	}
	unsigned long int r;
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &r);

	//if(r || !target.name) return r;

	return r;
}

void define(const char *d) {
	char buffer[2 + strlen(d) + 1];
	strcpy(buffer, "-D");
	strcpy(buffer + 2, d);
	add_arg(buffer);
}

void undefine(const char *u) {
	char buffer[2 + strlen(u) + 1];
	strcpy(buffer, "-U");
	strcpy(buffer + 2, u);
	add_arg(buffer);
}

static void add_paths(const char *path_set, void (*add)(const char *)) {
	char buffer[PATH_MAX + 1], *p = buffer;
	do {
		do {
			*p++ = *path_set++;
			if(p - buffer > PATH_MAX) {
				p = buffer;
				while(*++path_set != ';') if(!*path_set) return;
				path_set++;
			}
		} while(*path_set && *path_set != ';');
		*p = 0;
		add(buffer);
		p = buffer;
		if(*path_set) path_set++;
	} while(*path_set);
}

void add_include_path(const char *path) {
	char buffer[2 + strlen(path) + 1];
	strcpy(buffer, "-I");
	strcpy(buffer + 2, path);
	add_arg(buffer);
}

void add_library_path(const char *path) {
	char buffer[2 + strlen(path) + 1];
	strcpy(buffer, "-L");
	strcpy(buffer + 2, path);
	add_arg(buffer);
}

void add_library(const char *lib) {
	size_t len = strlen(lib);
	if(strncmp(lib + len - 4, ".lib", 4) == 0) len -= 4;
	char buffer[2 + len + 1];
	strcpy(buffer, "-l");
	memcpy(buffer + 2, lib, len);
	buffer[len] = 0;
	add_arg(buffer);
}

static const char *first_input_file;

void add_input_file(const char *file) {
	if(!first_input_file) first_input_file = file;
	add_arg(file);
}

void set_output_file(const char *file) {
	add_arg("-o");
	add_arg(file);
}

static void print_help() {
	puts("libdll.so cl2cc " VERSION);
	puts("Copyright 2015 libdll.so");
	puts("This is free software; published under the GNU GPL, version 2 or later.");
	puts("There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A");
	puts("PARTICULAR PURPOSE.\n");

	puts("Usage: cl2cc [<cl option> ...] <file> [...] [-link <link option> [...]]\n");

	puts("All options can be starts with - or /");
}

static struct warning_table {
	const char *name;
	unsigned int number;
} gcc_to_cl[] = {
	{ "implicit-function-declaration", 4013 },
	{ "unknown-pragmas", 4068 },
	{ "unused-parameter", 4100 },
	{ "unused-variable", 4101 },			// 未引用的局部变量
	{ "unused-label", 4102 },
	{ "unused-but-set-variable", 4189 },		// 局部变量已初始化但不引用
	{ "overloaded-virtual", 4264 },
	{ "implicit-int", 4431 },
	{ "undef", 4668 },
	{ "deprecated", 4996 },
	{ "deprecated-declarations", 4996 },
	{ "uninitialized", 4700 }
};

int disable_warning_by_number(unsigned int number) {
	if(number > 4999) return -1;
	int i;
	for(i = 0; i < sizeof gcc_to_cl / sizeof(struct warning_table); i++) {
		struct warning_table *p = gcc_to_cl + i;
		if(p->number == number) {
			char buffer[5 + strlen(p->name) + 1];
			memcpy(buffer, "-Wno-", 4);
			strcpy(buffer + 4, p->name);
			add_arg(buffer);
			return 1;
		}
	}
	return 0;
}

static int get_line_from_file(char *line, const char *file, size_t count) {
	fprintf(stderr, "function: get_line_from_file(%p, %p<%s>, %u)\n", line, file, file, (unsigned int)count);
	fprintf(stdout, "function: get_line_from_file(%p, %p<%s>, %u)\n", line, file, file, (unsigned int)count);
	fflush(stdout);
	char buffer[count];
	void *f = CreateFileA(file, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(!f) return -1;
	unsigned long int read_size;
	if(!ReadFile(f, buffer, count, &read_size, NULL)) return -1;
	//buffer[read_size / sizeof(char)] = 0;
	//wcstombs(line, buffer + 1, read_size);
	memcpy(line, buffer, read_size);
	int r = read_size;
	char *p = line;
	printf("read_size = %lu\n", read_size);
	fflush(stdout);
	while(p - line < read_size) {
		if(*p++ == '\r' && *p == '\n') {
			char *np = p;
			p[-1] = ' ';
			//while((*np = *++np));
			while((np[0] = np[1])) np++;
			r--;
		}
	}
	return r;
}

//static int get_arg_len(const char *s) {
//}

int expend_command_line_from_file(/*const char *file*/) {
#define BUFFER_SIZE (PATH_MAX * 4)

	STARTUPINFOA si = { .cb = sizeof(STARTUPINFOA) };
	PROCESS_INFORMATION pi;
	unsigned long int r;
	char program[PATH_MAX + 1];
	if(!GetModuleFileNameA(NULL, program, sizeof program)) {
		fprintf(stderr, "GetModuleFileNameA failed, error %lu\n", GetLastError());
		return 1;
	}
	char *old_command_line = GetCommandLineA();
	char new_command_line[BUFFER_SIZE], *p = new_command_line;
	//if(!get_line_from_file(new_))
	do {
		if(*old_command_line == '@') {
			//int old_len = get_arg_len(old_command_line)
			int old_len = 0;
			char file[PATH_MAX];
			old_command_line++;
			while(*old_command_line && *old_command_line != ' ' && *old_command_line != '	') {
				file[old_len++] = *old_command_line++;
			}
			file[old_len] = 0;
			int new_len = get_line_from_file(p, file, sizeof new_command_line - (p - new_command_line) - 1);
			if(new_len < 0) {
				fprintf(stderr, "failed to read command file %s, %lu\n", file, GetLastError());
				return 1;
			}
			p += new_len;
		} else *p++ = *old_command_line++;
	} while(*old_command_line);
	*p = 0;

	fprintf(stdout, "[stdout] old_command_line: \"%s\", program: \"%s\", new_command_line: \"%s\"\n", old_command_line, program, new_command_line);
	fprintf(stderr, "[stderr] old_command_line: \"%s\", program: \"%s\", new_command_line: \"%s\"\n", old_command_line, program, new_command_line);
	if(!CreateProcessA(program, new_command_line, NULL, NULL, 0, 0, NULL, NULL, &si, &pi)) {
		fprintf(stderr, "failed to restart %s, error %lu\n", program, GetLastError());
		return 1;
	}
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &r);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return r;
}

int main(int argc, char **argv) {
#define UNRECOGNIZED_OPTION(O) \
	{										\
		fprintf(stderr, "%s: warning: ignoring unknown option '%s'\n",		\
			argv[0], (O));							\
		break;									\
	}

#define CHECK_OPTION_AND_ITS_ARGUMENT(L,O,S) \
	{												\
		if((O)[L] && (O)[L] != S) UNRECOGNIZED_OPTION(*v);					\
		if(!(O)[L] || !(O)[(L)+1]) {								\
			fprintf(stderr, "%s: error: '%s' requires an argument\n", argv[0], *v);		\
			return 2;									\
		}											\
	}


	const char *include_path = getenv("INCLUDE");
	if(include_path) add_paths(include_path, add_include_path);
	const char *library_path = getenv("LIB");
	if(library_path) add_paths(library_path, add_library_path);

	int no_link = 0;
	int linker_option = 0;
	//int output_file_seted = 0;
	const char *output_file = NULL;
	char **v = argv;
	init_command_line();

	while(*++v) {
		if(**v == '@') {
			return expend_command_line_from_file(*v + 1);
		} else if(**v == '-' || **v == '/') {
			const char *arg = *v + 1;
			if(linker_option) {
				if(strcasecmp(arg, "debug") == 0) {
					add_arg("-g");
				} else if(strcasecmp(arg, "dll") == 0) {
					add_arg("--shared");
				} else if(strncasecmp(arg, "entry", 5) == 0) {
					if(arg[5] == ':') {
						const char *a = arg + 6;
						if(!*a) goto no_arg; 
						size_t alen = strlen(a);
						char ld_flag[7 + alen + 1];
						strcpy(ld_flag, "-Wl,-e,");
						strcpy(ld_flag + 7, a);
						add_arg(ld_flag);
					} else {
						if(!arg[5]) {
no_arg:
							fprintf(stderr, "%s: error: no argument specified with option '%s'",
								argv[0], *v);
							return 122;
						}
						while(1) UNRECOGNIZED_OPTION(*v);
					}
				} else if(strncasecmp(arg, "largeaddressaware", 17) == 0) {
					if(arg[17] == ':') {
						const char *a = arg + 18;
						if(strcasecmp(a, "no") == 0) {
							add_arg("-Wl,--no-large-address-aware");
						} else {
							fprintf(stderr, "%s: error: syntax error in option '%s'",
								argv[0], *v);
							return 93;
						}
					} else if(arg[17]) while(1) {
						UNRECOGNIZED_OPTION(*v);
					} else add_arg("-Wl,--large-address-aware");
				} else if(strcasecmp(arg, "nologo") == 0) {
					// Do nothing
				} else if(strncasecmp(arg, "subsystem", 9) == 0) {
					__label__ no_arg;
					// -Wl,--subsystem,
					if(arg[9] == ':') {
						const char *a = arg + 10;
						if(!*a) goto no_arg; 
						size_t alen = strlen(a);
						char ld_flag[16 + alen + 1];
						strcpy(ld_flag, "-Wl,--subsystem,");
						strcpy(ld_flag + 16, a);
						add_arg(ld_flag);
					} else {
						if(!arg[9]) {
no_arg:
							fprintf(stderr, "%s: error: no argument specified with option '%s'",
								argv[0], *v);
							return 122;
						}
						while(1) UNRECOGNIZED_OPTION(*v);
					}
				} else if(strcasecmp(arg, "wx") == 0) {
					add_arg("-Werror");
				} else while(1) UNRECOGNIZED_OPTION(*v);
			} else switch(*arg) {
				case 'c':
					if(arg[1]) UNRECOGNIZED_OPTION(*v);
					add_arg("-c");
					no_link = 1;
					break;
				case 'D':
					if(arg[1]) {
						if(**v != '-') **v = '-';
						add_arg(*v);
					} else {
						const char *d = *++v;
						if(!d) {
							fprintf(stderr, "%s: error: '%s' requires an argument\n", argv[0], *v);
							return 2;
						}
						define(d);
					}
					break;
				case 'E':
					if(arg[1] && (arg[1] != 'P' || arg[2])) UNRECOGNIZED_OPTION(*v);
					if(arg[1] == 'P') add_arg("-P");
					add_arg("-E");
					break;
				case 'F':
					switch(arg[1]) {
						case 0:
							fprintf(stderr, "%s: error: '%s' requires an argument\n", argv[0], *v);
							return 2;
						case 'e':
							if(no_link) break;
							//set_output_file(arg + 2);
							//output_file_seted = 1;
							output_file = arg + 2;
							break;
						case 'o':
							if(!no_link) {
								add_arg("-c");
								no_link = 1;
							}
							//set_output_file(arg + 2);
							//output_file_seted = 1;
							output_file = arg + 2;
							break;
						case 'a':
						case 'A':
						case 'd':
						case 'm':
						case 'p':
						case 'r':
						case 'R':
							fprintf(stderr, "%s: warning: '%s' is not supported", argv[0], *v);
							break;
						case 'I':
							if(!arg[2]) {
								fprintf(stderr, "%s: error: '%s' requires an argument\n", argv[0], *v);
								return 2;
							}
							add_arg("--include");
							add_arg(arg + 2);
							break;
						default:
							UNRECOGNIZED_OPTION(*v);
					}
					break;
				case 'G':
					switch(arg[1]) {
						case 'F':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("-fno-writable-strings");
							break;
						case 'X':
							if(arg[2]) {
								if(arg[2] == '-') add_arg("-fno-exceptions");
								else UNRECOGNIZED_OPTION(*v);
							} else add_arg("-fexceptions");
						case 'e':
						case 'Z':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("-fstack-check");
						default:
							UNRECOGNIZED_OPTION(*v);
					}
				case 'J':
					if(arg[1]) UNRECOGNIZED_OPTION(*v);
					add_arg("-funsigned-char");
					break;
				case 'L':
					//if(!arg[1]) UNRECOGNIZED_OPTION(*v);
					switch(arg[1]) {
						//case 0:
						//	UNRECOGNIZED_OPTION(*v);
						case 'D':
							if(arg[2] && arg[2] != 'd') UNRECOGNIZED_OPTION(*v);
							add_arg("--shared");
							break;
						case 'N':
							fprintf(stderr, "%s: error: '%s' is not supported\n",
								argv[0], *v);
							break;
						default:
							UNRECOGNIZED_OPTION(*v);
					}
					break;
				case 'M':
					switch(arg[1]) {
						case 'D':
							if(arg[2] && arg[2] != 'd') UNRECOGNIZED_OPTION(*v);
							break;
						case 'T':
							if(arg[2] && arg[2] != 'd') UNRECOGNIZED_OPTION(*v);
							fprintf(stderr, "%s: warning: linking with %s is not supported\n",
								argv[0], arg[2] ? "libcmtd" : "libcmt");
							break;
						default:
							UNRECOGNIZED_OPTION(*v);
					}
					break;
				case 'O':
					switch(arg[1]) {
						case 0:
							fprintf(stderr, "%s: warning: '%s' has been deprecated\n", argv[0], *v);
						case '1':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("-Os");
							break;
						case '2':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("-Ofast");
							break;
						case 'b': {
							const char *n = arg + 2;
							if(!*n) n = "0";
							char buffer[15 + strlen(n) + 1];
							strcpy(buffer, "-finline-limit=");
							strcpy(buffer + 15, n);
							add_arg(buffer);
							break;
						}
						case 'd':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("-O0");
							break;
						case 'g':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("-O2");
							break;
						case 'i':
							if(arg[2]) {
								if(arg[2] == '-') add_arg("-fno-builtin");
								else UNRECOGNIZED_OPTION(*v);
							}
							break;
						case 's':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("-Os");
							break;
						case 't':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("-Ofast");
							break;
						case 'x':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("-O3");
							break;
						case 'y':
							if(arg[2]) {
								if(arg[2] == '-') add_arg("-fno-omit-frame-pointer");
								else UNRECOGNIZED_OPTION(*v);
							} else add_arg("-fomit-frame-pointer");
							break;
						default:
							UNRECOGNIZED_OPTION(*v);
					}
				case 'T':
					switch(arg[1]) {
						case 0:
							fprintf(stderr, "%s: error: '%s' requires an argument\n", argv[0], *v);
							return 2;
						case 'c':
							add_arg("-xc");
							add_input_file(arg + 2);
							add_arg("-xnone");
							break;
						case 'p':
							add_arg("-xc++");
							add_input_file(arg + 2);
							add_arg("-xnone");
							break;
						default:
							UNRECOGNIZED_OPTION(*v);
					}
					break;
				case 'U':
					if(arg[1]) {
						if(**v != '-') **v = '-';
						add_arg(*v);
					} else {
						const char *d = *++v;
						if(!d) {
							fprintf(stderr, "%s: error: '%s' requires an argument\n", argv[0], *v);
							return 2;
						}
						undefine(d);
					}
					break;
				case 'u':
					if(arg[1]) UNRECOGNIZED_OPTION(*v);
					add_arg("-undef");
					break;
#if 0
				case 'v':	// For debugging
					if(arg[1]) UNRECOGNIZED_OPTION(*v);
					add_arg("-v");
					break;
#endif
				case 'W':
					switch(arg[1]) {
						case 0:
							fprintf(stderr, "%s: error: '%s' requires an argument\n", argv[0], *v);
							return 2;
						case '0':
							add_arg("-w");
							break;
						case '1':
						case '2':
							break;
						case '3':
							add_arg("-Wall");
							break;
						case '4':
							add_arg("-Wextra");
							break;
						case 'L':
							break;
						case 'X':
							add_arg("-Werror");
							break;
						default:
							if(strcmp(arg + 1, "all") == 0) {
								add_arg("-Wall");
								add_arg("-Wextra");
								break;
							}
							fprintf(stderr, "%s: error: invalid numeric argument '%s'", argv[0], *v);
							return 2;
					}
					break;
				case 'w':
					switch(arg[1]) {
						case 0:
							add_arg("-w");
							break;
						case 'd':
							if(!arg[2]) {
								fprintf(stderr, "%s: error: '%s' requires an argument", argv[0], *v);
								return 2;
							}
							int n = atoi(arg + 2);
							if(n < 0 || disable_warning_by_number(n) < 0) {
								fprintf(stderr, "%s: warning: invalid value '%d' for '-wd'; assuming '4999'",
									argv[0], n);
								return 2;
							}
							break;
						case 'e':
							if(!arg[2]) {
								fprintf(stderr, "%s: error: '%s' requires an argument", argv[0], *v);
								return 2;
							}
							fprintf(stderr, "%s: warning: '%s': %s\n", argv[0], *v, strerror(ENOSYS));
							break;
						case 'o':
						case '0' ... '9':
							fprintf(stderr, "%s: warning: '%s' is not supported\n", argv[0], *v);
							break;
						default:
							UNRECOGNIZED_OPTION(*v);
					}
					break;
				case 'X':
					if(arg[1]) UNRECOGNIZED_OPTION(*v);
					add_arg("-nostdinc");
					break;
				case 'Z':
					if(!arg[1]) {
						fprintf(stderr, "%s: error: '%s' requires an argument\n", argv[0], *v);
						return 2;
					}
					switch(arg[1]) {
						case 'i':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("-g");
							break;
						case 'a':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("--pedantic");
							break;
						case 'e':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							break;
						case 's':
							if(arg[2]) UNRECOGNIZED_OPTION(*v);
							add_arg("-fsyntax-only");
							break;
						default:
							UNRECOGNIZED_OPTION(*v);
					}
					break;
				help:
				case '?':
					print_help();
					return 0;
				default:
					if(strcmp(arg, "help") == 0) goto help;
					else if(strncmp(arg, "fp", 2) == 0) {
						CHECK_OPTION_AND_ITS_ARGUMENT(2, arg, ':');
						const char *m = arg + 3;
						if(strcmp(m, "fast") == 0) add_arg("-fexcess-precision=fast");
						else if(strcmp(m, "precise") == 0 || strcmp(m, "strict") == 0) {
							add_arg("-fexcess-precision=standard");
						}
					} else if(strncmp(arg, "arch", 4) == 0) {
						CHECK_OPTION_AND_ITS_ARGUMENT(4, arg, ':');
						const char *a = arg + 5;
						if(strcmp(a, "SSE") == 0) add_arg("-msse");
						else if(strcmp(a, "SSE2") == 0) add_arg("-msse2");
						else {
							fprintf(stderr, "%s: error: unrecognized architecture %s\n", argv[0], a);
							return 2;
						}
					} else if(strcmp(arg, "openmp") == 0) {
						add_arg("-fopenmp");
					} else if(strcmp(arg, "link") == 0) {
						linker_option = 1;
					} else if(strcmp(arg, "showIncludes") == 0) {
						add_arg("-M");
					} else if(strcmp(arg, "nologo") == 0) {
						// Do nothing
					} else {
						UNRECOGNIZED_OPTION(*v);
					}
					break;
			}
		} else {
			(linker_option ? add_library : add_input_file)(*v);
		}
	}
	if(!first_input_file) {
		fprintf(stderr, "%s: error: missing source filename\n", argv[0]);
		return 2;
	}
	if(!output_file) {
		size_t len = strlen(first_input_file);
		int n = get_last_dot(first_input_file, len);
		if(n >= 0) len = n;
		char *p = malloc(len + 5);
		if(!p) {
			perror(argv[0]);
			return 1;
		}
		memcpy(p, first_input_file, len);
		strcpy(p + len, no_link ? ".obj" : ".exe");
		output_file = p;
		//set_output_file(p);
	} else {
		size_t orig_len = strlen(output_file);
		if(output_file[orig_len - 1] == '/') {
			size_t add_len = strlen(first_input_file);
			int n = get_last_dot(first_input_file, add_len);
			if(n >= 0) add_len = n;
			char *p = malloc(orig_len + add_len + 5);
			if(!p) {
				perror(argv[0]);
				return 1;
			}
			memcpy(p, output_file, orig_len);
			memcpy(p + orig_len, first_input_file, add_len);
			strcpy(p + orig_len + add_len, no_link ? ".obj" : ".exe");
			output_file = p;
		}
	}
	set_output_file(output_file);
	puts(cc_command_line);
	return start_cc();
	//return 0;
}
