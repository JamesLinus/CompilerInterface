/*	cc2cl
	Copyright 2015 libdll.so

	This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#ifdef __INTERIX
#include <interix/interix.h>
#endif
#endif
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#ifndef DEFAULT_OUTPUT_FILENAME
#define DEFAULT_OUTPUT_FILENAME "a.exe"
#endif

#define VERSION "1.0"

#ifdef _MALLOC_NO_ERRNO
void *malloc1(size_t size) {
	void *r = malloc(size);
	if(!r) errno = ENOMEM;
	return r;
}

void *realloc1(void *p, size_t size) {
	void *r = realloc(p, size);
	if(!r) errno = ENOMEM;
	return r;
}

#define malloc malloc1
#define realloc realloc1
#endif

#if defined _WIN32 && !defined _WIN32_WCE
// Warning: Not POSIX compatible
static int setenv(const char *name, const char *value, int overwrite) {
	if(!overwrite && getenv(name)) return 0;
	size_t name_len = strlen(name);
	char buffer[name_len + 1 + strlen(value) + 1];
	memcpy(buffer, name, name_len);
	buffer[name_len] = '=';
	strcpy(buffer + name_len + 1, value);
	putenv(buffer);
	return 0;
}
#endif

static int cl_argc;
static char **cl_argv;

void init_argv() {
	cl_argc = 1;
	cl_argv = malloc(2 * sizeof(char *));
	if(!cl_argv) {
		perror(NULL);
		abort();
	}
	cl_argv[0] =
#ifdef _WIN32
		"cl.exe";
#else
		"cl";
#endif
	cl_argv[1] = NULL;
}

void add_to_argv(const char *arg) {
	cl_argc++;
	cl_argv = realloc(cl_argv, (cl_argc + 1) * sizeof(char *));
	if(!cl_argv) {
		perror(NULL);
		abort();
	}
	//cl_argv[cl_argc - 1] = arg;
	if(!(cl_argv[cl_argc - 1] = strdup(arg))) {
		perror(NULL);
		abort();
	}
	cl_argv[cl_argc] = NULL;
}

static int have_space(const char *s) {
	while(*s) {
		if(*s == ' ' || *s == '	') return 1;
		s++;
	}
	return 0;
}

void free_argv() {
	while(--cl_argc) free(cl_argv[cl_argc]);
	free(cl_argv);
}

void print_argv() {
	char **v = cl_argv;
	while(*v) {
		printf(have_space(*v) ? "\"%s\"" : "%s", *v);
		if(*++v) putchar(' ');
	}
	putchar('\n');
}

static int get_last_dot(const char *s, size_t len) {
	//size_t n = len;
	while(--len) if(s[len] == '.') break;
	if(!len) return -1;
	return len;
}

#define EXE 1
#define OBJ 2
static struct {
	const char *name;
	unsigned int type;
} target;

#ifdef _WIN32
static void add_to_path(const char *p) {
#define PATHS_SEPARATOR ';'
	static char *lpath;
	const char *path = getenv("PATH");
	if(!path) path = "";
	size_t old_path_len = strlen(path);
	size_t p_len = strlen(p);
	lpath = realloc(lpath, old_path_len + p_len + 2);
	if(!lpath) {
		perror(NULL);
		abort();
	}
	memcpy(lpath, "PATH=", 5);
	memcpy(lpath + 5, p, p_len);
	if(old_path_len) {
		lpath[5 + p_len] = PATHS_SEPARATOR;
		memcpy(lpath + 5 + p_len + 1, path, old_path_len + 1);
	} else lpath[5 + p_len] = 0;
	putenv(lpath);
}

static int argv_to_command_line(char **argv, char *command_line, size_t buffer_size) {
	size_t command_line_len = 0, len;
	char *p;
	do {
		p = command_line + command_line_len;
		*p++ = '\"';
		len = strlen(*argv);
		command_line_len += len + 1 + 2;	// The end of string *argv '\0' will be replace to ' '; and 2 quote chars.
		if(command_line_len > buffer_size) return -1;
		memcpy(p, *argv, len);
		p[len] = '\"';
		//if(!(p[len + 1] = *++argv ? ' ' : 0)) return 0;
		//if(!*++argv) break;
		//wcscat(p, L" ");
	} while((p[len + 1] = *++argv ? ' ' : 0));
	return 0;
}
#endif

// Call only once!
int start_cl() {
	const char *compiler = getenv("CL_LOCATION");
#ifdef _WIN32
	const char *vs_path = getenv("VS_PATH");
	if(!vs_path) vs_path = getenv("VSINSTALLDIR");
	if(vs_path) {
		size_t len = strlen(vs_path);
		if(vs_path[len - 1] == '/' || vs_path[len - 1] == '\\') len--; 
		char buffer[len + 8];
		memcpy(buffer, vs_path, len);
		strcpy(buffer + len, "/VC/bin");
		add_to_path(buffer);
	}
	char command_line[PATH_MAX * 2 + 1];
	argv_to_command_line(cl_argv, command_line, sizeof command_line);
	STARTUPINFOA si = { .cb = sizeof(STARTUPINFOA) };
	PROCESS_INFORMATION pi;
	while(!CreateProcessA(compiler, command_line, NULL, NULL, 0, 0, NULL, NULL, &si, &pi)) {
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

#else
	pid_t pid = fork();
	if(pid == -1) {
		perror("fork");
		abort();
	}
	if(pid == 0) {
		if(compiler) execvp(compiler, cl_argv);
		execvp("cl", cl_argv);
		perror("cl");
		exit(127);
	}
	free_argv();
	int status;
	if(waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		abort();
	}
	if(WIFSIGNALED(status)) {
		fprintf(stderr, "cl terminated with signal %d\n", WTERMSIG(status));
		exit(WTERMSIG(status) + 126);
	}
	int r = WEXITSTATUS(status);
#endif
	if(r || !target.name) return r;
	if(access(target.name, F_OK) == 0) return r;

	size_t len = strlen(target.name);
	int n = get_last_dot(target.name, len);
	if(n >= 0) len = n;
	char out[len + 4 + 1];
	memcpy(out, target.name, len);

	//out[len] = 0;
	//puts(target.name);
	//puts(out);

	assert(target.type == EXE || target.type == OBJ);
	strcpy(out + len, target.type == EXE ? ".exe" : ".obj");
	if(access(out, F_OK) < 0) {
		perror(NULL);
		return 1;
	}
/*
	if(target.type == EXE) {
		char manifest[len + 4 + 9 + 1];
		memcpy(manifest, out, len + 4);
		strcpy(manifest + len + 4, ".manifest");
		unlink(manifest);
	}*/
	return -rename(out, target.name);
}

static int no_static_link = 1;

struct option {
	const char *opt;
	char arg;
	void (*act)();
};

void define(const char *d) {
	char buffer[2 + strlen(d) + 1];
	strcpy(buffer, "-D");
	strcpy(buffer + 2, d);
	add_to_argv(buffer);
}

void undefine(const char *u) {
	char buffer[2 + strlen(u) + 1];
	strcpy(buffer, "-U");
	strcpy(buffer + 2, u);
	add_to_argv(buffer);
}

void include_file(const char *file) {
	char buffer[3 + strlen(file) + 1];
	strcpy(buffer, "-FI");
	strcpy(buffer + 3, file);
	add_to_argv(buffer);
}


void nostdinc() {
	putenv("INCLUDE=");		// Windows
	putenv("INCLUDE");		// POSIX
	//assert(!getenv("INCLUDE"));
	add_to_argv("-X");
}

//void nostdlib()

void static_link() {
	no_static_link = 0;
}

void make_dll() {
	add_to_argv("-LD");
}

void pedantic() {
	add_to_argv("-Za");
}

void language_standard(const char *std) {
	if(strcmp(std, "c89") == 0 || strcmp(std, "c90") == 0 || strcmp(std, "iso9899:1990") == 0 || strcmp(std, "iso9899:199409") == 0) add_to_argv("-Za");
	else if(strcmp(std, "ms") == 0 || strcmp(std, "msc") == 0 || strcmp(std, "msvc") == 0) add_to_argv("-Ze");
	else {
		fprintf(stderr, "error: unrecognized language standard '%s'.", std);
		exit(1);
	}
}

void undefine_system() {
	add_to_argv("-u");
}

void set_debug() {
	add_to_argv("-Zi");
}

void help(const char *name) {
	fprintf(stderr, "Usage: %s [<cc options>] <file> [...]\n", name);
	exit(0);
}

void cl_help() {
	add_to_argv("-?");
	start_cl();
	exit(0);
}

void version() {
	puts("libdll.so cc2cl " VERSION);
	puts("Copyright 2015 libdll.so");
	puts("This is free software; published under the GNU GPL, version 2 or later.");
	puts("There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A");
	puts("PARTICULAR PURPOSE.");
	exit(0);
}

static struct option singal_dash_long_options[] = {
	{ "pipe", 0, NULL },
	{ "ansi", 0, NULL },
	{ "include", 1, include_file },
	{ "nostdinc", 0, nostdinc },
//	{ "nostdinc++", 0, nostdinc_plus },
//	{ "nostartfile", 0, nostartfile },
//	{ "nostdlib", 0, nostdlib },
	{ "static", 0, static_link },
	{ "shared", 0, make_dll },
	{ "pedantic", 0, pedantic },
	{ "pedantic-error", 0, pedantic },
//	{ "save-temps", 0, save_temps },
	{ "std", 2, language_standard },
	{ "undef", 0, undefine_system }
};

static struct option double_dash_long_options[] = {
	{ "pipe", 0, NULL },
	{ "ansi", 0, NULL },
	{ "include", 1, include_file },
	{ "static", 0, static_link },
	{ "shared", 0, make_dll },
	{ "pedantic", 0, pedantic },
	{ "pedantic-error", 0, pedantic },
	{ "std", 1, language_standard },
	{ "undef", 1, undefine },
	{ "undefine", 1, undefine },
	{ "debug", 0, set_debug },
	{ "help", 0, help },
	{ "cl-help", 0, cl_help },
	{ "version", 0, version }
};

void add_include_path(const char *path) {
	char buffer[2 + strlen(path) + 1];
	strcpy(buffer, "-I");
	strcpy(buffer + 2, path);
	add_to_argv(buffer);
}

void add_library_path(const char *path) {
	static char *llib;
	const char *lib = getenv("LIB");
	if(!lib) lib = "";
	size_t old_path_len = strlen(lib);
	size_t new_path_len = strlen(path);
	llib = realloc(llib, old_path_len + new_path_len + 2);
	if(!llib) {
		perror(NULL);
		abort();
	}
	memcpy(llib, "LIB=", 4);
	memcpy(llib + 4, path, new_path_len);
	if(old_path_len) {
		llib[4 + new_path_len] = ';';
		memcpy(llib + 4 + new_path_len + 1, path, old_path_len + 1);
	} else llib[4 + new_path_len] = 0;
	putenv(llib);
}

static const char **libs;
static unsigned int libs_count;

void add_library(const char *lib) {
	libs = realloc(libs, ++libs_count * sizeof(char *));
	if(!libs) {
		perror(NULL);
		abort();
	}
	libs[libs_count - 1] = lib;
}

void add_libraries_to_argv() {
	int i;
	if(!libs_count) return;
	add_to_argv("-link");
	for(i=0; i<libs_count; i++) {
		size_t len = strlen(libs[i]);
		char buffer[len + 4 + 1];
		memcpy(buffer, libs[i], len);
		strcpy(buffer + len, ".lib");
		add_to_argv(buffer);
	}
}

void set_feature(const char *feature) {
	if(strcmp(feature, "no-builtin") == 0 || strcmp(feature, "no-builtin-function") == 0) add_to_argv("-Oi-");
	else if(strcmp(feature, "openmp") == 0) add_to_argv("-openmp");
	else if(strcmp(feature, "ms-extensions") == 0) add_to_argv("-Ze");
	else if(strcmp(feature, "unsigned-char") == 0 || strcmp(feature, "no-signed-char") == 0) add_to_argv("-J");
	else if(strcmp(feature, "no-writable-strings") == 0) add_to_argv("-GF");
	else if(strcmp(feature, "syntax-only") == 0) add_to_argv("-Zs");
	else if(strcmp(feature, "stack-check") == 0) add_to_argv("-GZ");
	else if(strcmp(feature, "omit-frame-pointer") == 0) add_to_argv("-Oy");
	else if(strcmp(feature, "no-omit-frame-pointer") == 0) add_to_argv("-Oy-");
	else if(strcmp(feature, "exceptions") == 0) add_to_argv("-EHs");
	else if(strncmp(feature, "excess-precision=", 17) == 0) {
		const char *a = feature + 17;
		if(strcmp(a, "fast") == 0) add_to_argv("-fp:fast");
		else if(strcmp(a, "standard") == 0) add_to_argv("-fp:precise");
		else {
			fprintf(stderr, "error: unknown excess precision style '%s'\n", a);
			exit(4);
		}
	} else if(strncmp(feature, "inline-limit=", 13) == 0) {
		const char *n = feature + 13;
		size_t n_len = strlen(n);
		char buffer[3 + n_len + 1];
		strcpy(buffer, "-Ob");
		memcpy(buffer + 3, n, n_len);
		buffer[3 + n_len] = 0;
		add_to_argv(buffer);
	} else fprintf(stderr, "warning: unrecognized feature %s\n", feature);
}

void set_machine(const char *machine) {
	if(strcmp(machine, "sse") == 0) add_to_argv("-arch:SSE");
	else if(strcmp(machine, "sse2") == 0) add_to_argv("-arch:SSE2");
	else {
		fprintf(stderr, "error: unrecognized machine %s\n", machine);
		exit(4);
	}
}

void disable_warning_by_number(unsigned int number) {
	char buffer[3 + 4 + 1];
	if(number > 9999) return;
	sprintf(buffer, "-wd%u", number);
	add_to_argv(buffer);
}

void disable_warning(const char *w) {
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
	int i;
	for(i = 0; i < sizeof gcc_to_cl / sizeof(struct warning_table); i++) {
		struct warning_table *p = gcc_to_cl + i;
		if(strcmp(w, p->name) == 0) {
			disable_warning_by_number(p->number);
			return;
		}
	}
}

int set_warning(const char *w) {
	if(strcmp(w, "error") == 0 || strcmp(w, "fatal-errors") == 0) add_to_argv("-WX");
	else if(strcmp(w, "extra") == 0) add_to_argv("-Wall");
	else if(strncmp(w, "no-", 3) == 0) disable_warning(w + 3);
	else if(((*w <= '4' && *w >= '0') || *w == 'L') && !w[1]) return 0;
	//else if(!*w) {
	//	fprintf(stderr, "warning: option '-W' is deprecated; use '-Wextra' instead\n");
	//	add_to_argv("-Wall");
	//}
	else return -1;
	return 1;
}

static const char *last_language;

void set_language(const char *lang) {
	if(strcmp(lang, "none") == 0) {
		last_language = NULL;
		return;
	}
	if(strcmp(lang, "c") && strcmp(lang, "c++")) {
		fprintf(stderr, "error: language %s not recognized\n", lang);
		exit(1);
	}
	last_language = lang;
}

static const char *first_input_file;

void add_input_file(char *file) {
	if(!first_input_file) first_input_file = file;
	if(last_language) {
		char buffer[3 + strlen(file) + 1];
		assert(strcmp(last_language, "c") == 0 || strcmp(last_language, "c++") == 0);
		sprintf(buffer, "-T%c%s", last_language[1] ? 'p' : 'c', file);
		add_to_argv(buffer);
		//last_language = NULL;
		return;
	}
	if(*file == '/') *file = '\\';
	add_to_argv(file);
}

void set_output_file(const char *file, int no_link) {
	char buffer[3 + strlen(file) + 1];
	sprintf(buffer, "-F%c%s", no_link ? 'o' : 'e', file);
	add_to_argv(buffer);
	target.name = file;
	target.type = no_link ? OBJ : EXE;
}

static int find_argv(char **v, const char *s) {
	while(*++v) if(strcmp(*v, s) == 0) return 1;
	return 0;
}

int main(int argc, char **argv) {
#define FIND_LONG_OPTION(ARRAY) \
	{															\
		for(i = 0; i < sizeof (ARRAY) / sizeof(struct option); i++) {							\
			struct option *o = (ARRAY) + i;										\
			if(o->arg < 2) {											\
				if(strcmp(arg, o->opt) == 0) {									\
					const char *a = o->arg ? *++v : argv[0];						\
					if(o->arg && !a) {									\
						fprintf(stderr, "%s: option '%s' need an argument\n", argv[0], *v);		\
						return -1;									\
					}											\
					if(o->act) o->act(a);									\
					goto first_loop;									\
				}												\
			} else {												\
				size_t len = strlen(o->opt);									\
				if(strncmp(arg, o->opt, len) == 0) {								\
					if(arg[len] && arg[len] != '=') continue;						\
					if(!arg[len] || !arg[len + 1]) {							\
						fprintf(stderr, "%s: option '%s' need an argument\n", argv[0], *v);		\
						return -1;									\
					}											\
					const char *a = arg + len + 1;								\
					if(o->act) o->act(a);									\
					goto first_loop;									\
				}												\
			}													\
		}														\
	}

#define UNRECOGNIZED_OPTION(O) \
	do {									\
		fprintf(stderr, "%s: error: unrecognized option '%s'\n",	\
			argv[0], (O));						\
		return -1;							\
	} while(0)


	int verbose = 0;
	int no_link = 0;
	int preprocess_only = 0;
	//int have_input_file = 0;
	int end_of_options = 0;
	const char *output_file = DEFAULT_OUTPUT_FILENAME;
	char **v = argv;
	init_argv();

	int no_warning = -1;
	const char *vs_path = getenv("VS_PATH");
	if(!vs_path) vs_path = getenv("VSINSTALLDIR");
	if(!getenv("INCLUDE")) {
		if(vs_path) {
			size_t len = strlen(vs_path);
			if(vs_path[len - 1] == '/' || vs_path[len - 1] == '\\') len--; 
			char buffer[len + 12 + len + 24 + 1];
			memcpy(buffer, vs_path, len);
			memcpy(buffer + len, "/VC/include;", 12);
			memcpy(buffer + len + 12, vs_path, len);
			strcpy(buffer + len + 12 + len, "/VC/PlatformSDK/include;");
			setenv("INCLUDE", buffer, 0);
		} else {
			no_warning = find_argv(argv, "-w");
			if(!no_warning) fprintf(stderr, "%s: warning: no system include path set\n", argv[0]);
		}
	}
	if(!getenv("LIB")) {
		if(vs_path) {
			size_t len = strlen(vs_path);
			if(vs_path[len - 1] == '/' || vs_path[len - 1] == '\\') len--; 
			char buffer[len + 8 + len + 20 + 1];
			memcpy(buffer, vs_path, len);
			memcpy(buffer + len, "/VC/lib;", 8);
			memcpy(buffer + len + 12, vs_path, len);
			strcpy(buffer + len + 12 + len, "/VC/PlatformSDK/lib;");
			setenv("LIB", buffer, 0);
		} else {
			if(no_warning == -1) no_warning = find_argv(argv, "-w");
			if(!no_warning) fprintf(stderr, "%s: warning: no system library path set\n", argv[0]);
		}
	}

first_loop:
	while(*++v) {
		if(!end_of_options && **v == '-') {
			int i;
			if((*v)[1] == '-') {
				const char *arg = *v + 2;
				if(!*arg) {
					end_of_options = 1;
					continue;
				}
				FIND_LONG_OPTION(double_dash_long_options);
				if(strcmp(arg, "verbose") == 0) {
					verbose = 1;
				} else UNRECOGNIZED_OPTION(*v);
			} else {
				const char *arg = *v + 1;
				FIND_LONG_OPTION(singal_dash_long_options);
				switch(*arg) {
					case 0:
						goto not_an_option;
					case 'c':
						if(arg[1]) UNRECOGNIZED_OPTION(*v);
						add_to_argv("-c");
						no_link = 1;
						break;
					case 'D':
						if(arg[1]) add_to_argv(*v);
						else {
							const char *d = *++v;
							if(!d) {
								fprintf(stderr, "%s: error: macro name missing after '-D'\n",
									argv[0]);
								return -1;
							}
							define(d);
						}
						break;
					case 'E':
						if(arg[1]) UNRECOGNIZED_OPTION(*v);
						add_to_argv("-E");
						preprocess_only = 1;						
						break;
					case 'f':
						if(arg[1]) set_feature(arg + 1);
						else {
							const char *feature = *++v;
							if(!feature) {
								fprintf(stderr, "%s: error: option '-f' need an argument\n",
									argv[0]);
								return -1;
							}
							set_feature(feature);
						}
						break;
					case 'g':
						if(arg[1] && strcmp(arg + 1, "coff")) {
							fprintf(stderr, "%s: error: unrecognised debug output level \"%s\"\n",
								argv[0], arg + 1);
							return 1;
						}
						add_to_argv("-Zi");
						break;
					case 'I':
						if(arg[1]) add_to_argv(*v);
						else {
							const char *path = *++v;
							if(!path) {
								fprintf(stderr, "%s: error: option '-I' need an argument\n",
									argv[0]);
								return -1;
							}
							add_include_path(path);
						}
						break;
					case 'L':
						if(arg[1]) add_library_path(arg + 1);
						else {
							const char *path = *++v;
							if(!path) {
								fprintf(stderr, "%s: error: option '-L' need an argument\n",
									argv[0]);
								return -1;
							}
							add_library_path(path);
						}
						break;
					case 'l':
						if(arg[1]) add_library(arg + 1);
						else {
							const char *path = *++v;
							if(!path) {
								fprintf(stderr, "%s: error: option '-l' need an argument\n",
									argv[0]);
								return -1;
							}
							add_library(path);
						}
						break;
					case 'M':
						if(arg[1]) UNRECOGNIZED_OPTION(*v);
						add_to_argv("-showIncludes");
						break;
					case 'm':
						if(arg[1]) set_machine(arg + 1);
						else {
							const char *machine = *++v;
							if(!machine) {
								fprintf(stderr, "%s: argument to `-m' is missing\n",
									argv[0]);
								return 1;
							}
							set_machine(machine);
						}
						break;
					case 'O':
						if(arg[1]) {
							const char *o = arg + 1;
							if(strcmp(o, "0") == 0) add_to_argv("-Od");
							else if(strcmp(o, "1") == 0) add_to_argv("-O2");
							else if(strcmp(o, "3") == 0) add_to_argv("-Ox");
							else if(strcmp(o, "s") == 0) add_to_argv("-O1");
							else if(strcmp(o, "fast") == 0) add_to_argv("-O2");
							else add_to_argv(*v);
						} else add_to_argv("-O2");
						break;
					case 'o':
						if(arg[1]) output_file = arg + 1;
						else {
							output_file = *++v;
							if(!output_file) {
								fprintf(stderr, "%s: error: option '-o' need an argument\n",
									argv[0]);
								return -1;
							}
						}
						break;
					case 'P':
						if(preprocess_only) add_to_argv("-EP");
						break;
					case 's':
						if(arg[1]) UNRECOGNIZED_OPTION(*v);
						//add_to_argv("-Y-");
						break;
					case 'U':
						if(arg[1]) add_to_argv(*v);
						else {
							const char *u = *++v;
							if(!u) {
								fprintf(stderr, "%s: error: macro name missing after '-U'\n",
									argv[0]);
								return -1;
							}
							undefine(u);
						}
						break;
					case 'v':
						if(arg[1]) UNRECOGNIZED_OPTION(*v);
						verbose = 1;
						break;
					case 'W':
						if(!arg[1]) {
							if(no_warning == -1) no_warning = find_argv(argv, "-w");
							if(!no_warning) {
								fprintf(stderr, "%s: warning: option '-W' is deprecated; use '-Wextra' instead\n",
									argv[0]);
							}
							add_to_argv("-Wall");
							break;
						}
						if(strncmp(arg, "Wa,", 3) == 0 || strncmp(arg, "Wp,", 3) == 0 || strncmp(arg, "Wl,", 3) == 0) {
							(*v)[3] = 0;		// XXX
							fprintf(stderr, "%s: warning: option '%s' is not supported\n", argv[0], *v);
							break;
						} 
						if(set_warning(arg + 1)) break;
						add_to_argv(*v);
						break;
					case 'w':
						if(arg[1]) UNRECOGNIZED_OPTION(*v);
						add_to_argv("-w");
						break;
					case 'x':
						if(arg[1]) set_language(arg + 1);
						else {
							const char *lang = *++v;
							if(!lang) {
								fprintf(stderr, "%s: error: missing argument to ‘-x’",
									argv[0]);
								return 4;
							}
							set_language(lang);
						}
						//if(!v[1])
						break;
					default:
						fprintf(stderr, "%s: error: unrecognized option '%s'\n", argv[0], *v);
						return -1;
				}
			}
		} else {
not_an_option:
#if defined __INTERIX && !defined _NO_CONV_PATH
			if(**v == '/') {
				char buffer[PATH_MAX];
				if(unixpath2win(*v, 0, buffer, sizeof buffer) == 0) {
					add_input_file(buffer);
				} else {
					if(no_warning == -1) no_warning = find_argv(argv, "-w");
					if(!no_warning) {
						fprintf(stderr, "%s: warning: cannot convert '%s' to Windows path name, %s\n",
							argv[0], *v, strerror(errno));
					}
					add_input_file(*v);
				}
			} else
#endif
			add_input_file(*v);
		}
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	if(last_language) {
		if(no_warning == -1) no_warning = find_argv(argv, "-w");
		if(!no_warning) fprintf(stderr, "%s: warning: '-x %s' after last input file has no effect\n", argv[0], last_language);
	}
	if(!first_input_file) {
		if(verbose) {
			if(!no_link) add_to_argv("-c");
			start_cl();
			return 0;
		}
		fprintf(stderr, "%s: no input files\n", argv[0]);
		return 1;
	}
	if(no_link && !output_file) {
		size_t len = strlen(first_input_file);
		int n = get_last_dot(first_input_file, len);
		if(n >= 0) len = n;
		char *p = malloc(len + 3);
		if(!p) {
			perror(argv[0]);
			return 1;
		}
		memcpy(p, first_input_file, len);
		strcpy(p + len, ".o");
		output_file = p;
		//char buffer[len + 3];
		//memcpy(buffer, first_input_file, len);
		//strcpy(buffer, ".o");
		//set_output_file(buffer, no_link);
	}
	if(!verbose) add_to_argv("-nologo");
	set_output_file(output_file, no_link);
	//if(no_static_link) add_to_argv("-MD");
	add_to_argv(no_static_link ? "-MD" : "-MT");
	add_libraries_to_argv();
	if(verbose) print_argv();
	return start_cl();
}
