/* Copyright 2011 secunet AG
 * written by Patrick Georgi <patrick.georgi@secunet.com>
 *
 * Licensed as BSD-L to ease migration to libpayload */

#ifndef __DIRENT_H
#define __DIRENT_H
struct dirent {
	char *d_name;
};

typedef struct {
	struct dirent **items;
	struct dirent **cur;
} DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp); // 0 on success, -1 on error, with errno set

int scandir(const char *path, struct dirent ***namelist,
		int (*filter)(const struct dirent *),
		int (*compar)(const struct dirent **, const struct dirent **));

int alphasort(const struct dirent **a, const struct dirent **b);

#endif

