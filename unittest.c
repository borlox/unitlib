#include <stdlib.h>
#include <string.h>
#include "unitlib.h"
#include "intern.h"

#define BEGIN_TEST(name) \
	{ printf("------ [ %-20s ]\n", name); const char * _test_name = name; int _fails=0;

#define END_TEST \
	if (_fails) printf(":%d error%s\n", _fails, _fails > 1 ? "s" : ""); \
	else printf(":Ok\n"); \
	printf("-------------------------------\n");}

#define CHECK(expr) \
	do { if (!(expr)) { printf("%s failed.\n", #expr); _fails++; }} while (0)

int main(void)
{
	printf("Unit tests for " UL_FULL_NAME "\n");

	ul_debugging(false);
	ul_init();

	BEGIN_TEST("Parser I")
		unit_t u;
		CHECK(ul_parse("m", &u));
		CHECK(u.exps[U_METER] == 1);

		int i=0;
		for (; i < NUM_BASE_UNITS; ++i) {
			if (i != U_METER) {
				CHECK(u.exps[i] == 0);
			}
		}

		CHECK(ncmp(u.factor, 1.0) == 0);
	END_TEST

	BEGIN_TEST("Parser II")
		unit_t u;
		CHECK(ul_parse("	\n kg^2 * m  ", &u));
		CHECK(u.exps[U_KILOGRAM] == 2);
		CHECK(u.exps[U_METER] == 1);
		CHECK(u.exps[U_SECOND] == 0);
		CHECK(ncmp(u.factor, 1.0) == 0);
	END_TEST

	BEGIN_TEST("Parser III")
		unit_t u;
		CHECK(ul_parse("2 Cd 7 s^-1", &u));
		CHECK(u.exps[U_CANDELA] == 1);
		CHECK(u.exps[U_SECOND] == -1);
		CHECK(ncmp(u.factor, 14.0) == 0);
	END_TEST

	BEGIN_TEST("Parser IV")
		unit_t u;

		const char *strings[] = {
			"5*kg^2", "5 ** kg^2", "5! * kg^2", "5 * kg^2!", NULL
		};

		int i = 0;
		while (strings[i]) {
			CHECK(ul_parse(strings[i], &u) == false);
			i++;
		}
	END_TEST

}