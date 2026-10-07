/* t.h - tiny assertion helpers for the unit tests. */
#ifndef SLATEKBD_T_H
#define SLATEKBD_T_H
#include <stdio.h>
#include <stdlib.h>

static int t_fail;
static int t_checks;
#define CHECK(cond) do { t_checks++; if (!(cond)) { t_fail++; \
	fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECKF(cond, ...) do { t_checks++; if (!(cond)) { t_fail++; \
	fprintf(stderr, "%s:%d: CHECK failed: %s: ", __FILE__, __LINE__, #cond); fprintf(stderr, __VA_ARGS__); \
	fputc('\n', stderr); } } while (0)
#define T_DONE() do { printf("%d checks, %d failures\n", t_checks, t_fail); return t_fail ? 1 : 0; } while (0)
#endif
