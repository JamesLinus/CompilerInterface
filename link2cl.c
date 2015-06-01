/*	link2cl
	Copyright 2015 libdll.so

	This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#include <windows.h>
#include <stdio.h>

#define BUF_SIZE 1024

char *GetLineFromFile(char *line, const char *file) {
	char buffer[BUF_SIZE];
	void *f = CreateFileA(file, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(!f) return NULL;
	unsigned long int read_size;
	if(!ReadFile(f, (char *)buffer, BUF_SIZE, &read_size, NULL)) return NULL;
	//buffer[read_size / sizeof(wchar_t)] = 0;
	//wcstombs(line, buffer + 1, read_size);
	memcpy(line, buffer, read_size);
	//int r = read_size;
	char *p = line;
	while(*p) {
		if(*p++ == '\r' && *p == '\n') {
			char *np = p;
			p[-1] = ' ';
			//while((*np = *++np));
			while((np[0] = np[1])) np++;
		}
	}
	return line;
}

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE prev_instance, char *command_line, int show) {
	puts(command_line);
	//while(*command_line != ' ') if(!*command_line++) return -1;
	STARTUPINFOA si = { .cb = sizeof(STARTUPINFOA) };
	PROCESS_INFORMATION pi;
	char cl_command_line[13 + BUF_SIZE];
	strcpy(cl_command_line, "cl.exe -link ");
	if(*command_line == '@') {
		if(!GetLineFromFile(cl_command_line + 13, command_line + 1)) {
			fprintf(stderr, "failed to read command file %s, %lu\n", command_line + 1, GetLastError());
			return 1;
		}
	} else strcpy(cl_command_line + 13, command_line);
	puts(cl_command_line);
	while(!CreateProcessA(NULL, cl_command_line, NULL, NULL, 0, 0, NULL, NULL, &si, &pi)) {
		fprintf(stderr, "CreateProcessA failed, error %lu\n", GetLastError());
		return 1;
	}
	unsigned long int r;
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &r);
	return r;
}
