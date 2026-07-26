#pragma once

#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>

static ssize_t codewizard_getline(char** line, size_t* capacity, FILE* file) {
	if (!line || !capacity || !file) return -1;

	if (!*line || *capacity == 0) {
		*capacity = 256;
		*line = (char*)malloc(*capacity);
		if (!*line) return -1;
	}

	size_t length = 0;
	int ch = 0;
	while ((ch = fgetc(file)) != EOF) {
		if (length + 1 >= *capacity) {
			size_t next_capacity = *capacity * 2;
			char* next = (char*)realloc(*line, next_capacity);
			if (!next) return -1;
			*line = next;
			*capacity = next_capacity;
		}
		(*line)[length++] = (char)ch;
		if (ch == '\n') break;
	}

	if (length == 0 && ch == EOF) return -1;
	(*line)[length] = '\0';
	return (ssize_t)length;
}

#define getline codewizard_getline

#endif
