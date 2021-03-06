#ifndef GET_TEST_DEFS
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include "unitlib.h"

// yay, self include (-:
#define GET_TEST_DEFS
#include "unittest.c"
#undef GET_TEST_DEFS

static inline int ncmp(ul_number a, ul_number b)
{
	if (_fabsn(a-b) < N_EPSILON)
		return 0;
	if (a < b)
		return -1;
	return 1;
}

static unit_t make_unit(ul_number fac, ...)
{
	va_list args;
	va_start(args, fac);

	unit_t u;
	memset(u.exps, 0, NUM_BASE_UNITS * sizeof(int));
	u.factor = fac;

	int b = va_arg(args, int);
	int e = va_arg(args, int);

	while (b || e) {
		u.exps[b] = e;
		b = va_arg(args, int);
		e = va_arg(args, int);
	}

	return u;
}
#define MAKE_UNIT(...) make_unit(__VA_ARGS__,0,0,0)

AUTO_FAIL
	printf("[%s-%d-%d] The test '%s' failed: \n[%s-%d-%d] Error message: %s\n", Suite, Test, Check, Expr, Suite, Test, Check, ul_error());
END_AUTO_FAIL

TEST_SUITE(parser)

	GROUP("base")
		TEST
			unit_t u;
			CHECK(ul_parse("m", &u));
			FAIL_MSG("Error: %s", ul_error());
			CHECK(u.exps[U_METER] == 1);

			int i=0;
			for (; i < NUM_BASE_UNITS; ++i) {
				if (i != U_METER) {
					CHECK(u.exps[i] == 0);
				}
			}

			CHECK(ncmp(u.factor, 1.0) == 0);
		END_TEST

		TEST
			unit_t u;

			CHECK(ul_parse("	\n kg^2 * m  ", &u));
			FAIL_MSG("Error: %s", ul_error());
			CHECK(u.exps[U_KILOGRAM] == 2);
			CHECK(u.exps[U_METER] == 1);
			CHECK(u.exps[U_SECOND] == 0);
			CHECK(ncmp(u.factor, 1.0) == 0);

			CHECK(ul_parse("2 Cd 7 s^-1", &u));
			FAIL_MSG("Error: %s", ul_error());
			CHECK(u.exps[U_CANDELA] == 1);
			CHECK(u.exps[U_SECOND] == -1);
			CHECK(ncmp(u.factor, 14.0) == 0);

			CHECK(ul_parse("", &u));
			int i=0;
			for (; i < NUM_BASE_UNITS; ++i) {
				CHECK(u.exps[i] == 0);
			}
			CHECK(ncmp(u.factor, 1.0) == 0);
		END_TEST
	END_GROUP()

	GROUP("validation")
		TEST
			unit_t u;

			const char *strings[] = {
				"5 ** kg^2", // double *
				"5! * kg^2", // !
				"5 * kg^2!", // !
				"sqrt kg^2)", // missing ( after sqrt
				"( kg^2 m",   // missing )
				"((((((((((((((((((((((((((((((((((((((((((((((((",
				NULL
			};

			int i = 0;
			while (strings[i]) {
				CHECK(ul_parse(strings[i], &u) == false);
				PASS_MSG("Error message: %s", ul_error());
				FAIL_MSG("'%s' is invalid but the parser reports no error.", strings[i]);
				i++;
			}
		END_TEST

		TEST
			const char *strings[] = {
				"",          // empty rule
				" =",        // empty symbol
				"16 = 16",   // invalid rule
				" a b = s ", // invalid symbol
				" c == kg",  // double =
				"d = e",     // unknown 'e'
				" = kg",     // empty symbol
				NULL,
			};

			int i=0;
			while (strings[i]) {
				CHECK(ul_parse_rule(strings[i]) == false);
				PASS_MSG("Error message: %s", ul_error());
				FAIL_MSG("'%s' is invalid but the parser reports no error.", strings[i]);
				i++;
			}
		END_TEST

		TEST
			unit_t u;

			CHECK(ul_parse(NULL, NULL) == false);
			CHECK(ul_parse(NULL, &u)   == false);
			CHECK(ul_parse("kg", NULL) == false);

			CHECK(ul_parse_rule(NULL) == false);
			CHECK(ul_parse_rule("")   == false);
		END_TEST

		TEST
			// Empty rules are allowed
			CHECK(ul_parse_rule("EmptySymbol = "));
			FAIL_MSG("Error: %s", ul_error());

			unit_t u;
			CHECK(ul_parse("EmptySymbol", &u));
			FAIL_MSG("Error: %s", ul_error());

			int i=0;
			for (; i < NUM_BASE_UNITS; ++i) {
				CHECK(u.exps[i] == 0);
			}
			CHECK(ncmp(u.factor, 1.0) == 0);
		END_TEST
	END_GROUP()

	TEST
		unit_t kg = MAKE_UNIT(1.0, U_KILOGRAM, 1);
		unit_t s  = MAKE_UNIT(1.0, U_SECOND, 1);

		unit_t u;

		CHECK(ul_parse_rule("!ForcedRule = kg"));
		FAIL_MSG("Error: %s", ul_error());
		CHECK(ul_parse("ForcedRule", &u));
		FAIL_MSG("Error: %s", ul_error());
		CHECK(ul_equal(&kg, &u));

		CHECK(ul_parse_rule("NewRule = kg"));
		FAIL_MSG("Error: %s", ul_error());
		CHECK(ul_parse("NewRule", &u));
		FAIL_MSG("Error: %s", ul_error());
		CHECK(ul_equal(&kg, &u));

		CHECK(ul_parse_rule("!NewRule = s"));
		FAIL_MSG("Error: %s", ul_error());
		CHECK(ul_parse("NewRule", &u));
		CHECK(ul_equal(&s, &u));

		CHECK(ul_parse_rule("!NewRule = m") == false);
		CHECK(ul_parse_rule("!kg = kg") == false);

		CHECK(ul_parse_rule(" Recurse = m"));
		FAIL_MSG("Error: %s", ul_error());
		CHECK(ul_parse_rule("!Recurse = Recurse") == false);
	END_TEST

	TEST
		static char prefs[] = "YZEPTGMkh dcmunpfazy";
		static ul_number factors[] = {
			1e24, 1e21, 1e18, 1e15, 1e12, 1e9, 1e6, 1e3, 1e2, 1,
			1e-1, 1e-2, 1e-3, 1e-6, 1e-9, 1e-12, 1e-15, 1e-18, 1e-21, 1e-24,
		};

		size_t num_prefs = strlen(prefs);
		FATAL((sizeof(factors) / sizeof(factors[0])) != num_prefs);

		for (size_t i = 0; i < num_prefs; ++i) {
			char expr[128] = "";
			snprintf(expr, 128, "5 %cm", prefs[i]);

			unit_t u;
			CHECK(ul_parse(expr, &u));
			FAIL_MSG("Failed to parse: '%s' (%s)", expr, ul_error());

			CHECK(ncmp(ul_factor(&u), 5 * factors[i]) == 0);
			FAIL_MSG("Factor: %g instead of %g (%c)", ul_factor(&u), 5 * factors[i], prefs[i]);

			// check kilogram, the only base unit with a prefix
			snprintf(expr, 128, "%cg", prefs[i]);
			CHECK(ul_parse(expr, &u));
		}
	END_TEST

	TEST
		unit_t correct = MAKE_UNIT(1.0, U_KILOGRAM, 1, U_SECOND, -1);

		unit_t test;
		CHECK(ul_parse("kg / s", &test));
		FAIL_MSG("Error: %s", ul_error());

		CHECK(ul_equal(&test, &correct));

		correct.factor = 2.0;
		CHECK(ul_parse("8 kg / 4 s", &test));
		CHECK(ul_equal(&test, &correct));
	END_TEST

	GROUP("extended")
		TEST
			unit_t kg = MAKE_UNIT(2.0, U_KILOGRAM, 1);
			unit_t u;

			CHECK(ul_parse("sqrt(4 kg^2)", &u));
			CHECK(ul_equal(&u, &kg));
			char buffer[128];
			ul_snprint(buffer, 128, &u, UL_FMT_PLAIN, 0);
			FAIL_MSG("Result was: %s", buffer);
		END_TEST

		TEST
			unit_t correct = MAKE_UNIT(1.0, U_METER, -1, U_KILOGRAM, 2);
			unit_t u;

			CHECK(ul_parse("sqrt(kg^2/m^2) kg", &u));
			CHECK(ul_equal(&u, &correct));
		END_TEST

		TEST
			unit_t correct = MAKE_UNIT(1.0, U_METER, 2, U_SECOND, 2);
			unit_t u;

			CHECK(ul_parse("(m s)^2", &u));
			CHECK(ul_equal(&u, &correct));
		END_TEST

		TEST
			unit_t u;
			CHECK(ul_parse("kg*m^2/(s^4 kg) sqrt(A^2 K^4)", &u));
		END_TEST
	END_GROUP()
END_TEST_SUITE()

TEST_SUITE(core)
	TEST
		unit_t kg = MAKE_UNIT(1.0, U_KILOGRAM, 1);
		unit_t kg2 = MAKE_UNIT(1.0, U_KILOGRAM, 1);

		CHECK(ul_equal(&kg, &kg2));

		kg2.factor = 2.0;
		CHECK(!ul_equal(&kg, &kg2));

		kg2.factor = 1.0;
		CHECK(ul_equal(&kg, &kg2));
		kg2.exps[U_KILOGRAM]++;
		CHECK(!ul_equal(&kg, &kg2));

		unit_t N = MAKE_UNIT(1.0, U_KILOGRAM, 1, U_SECOND, -2, U_METER, 1);
		CHECK(!ul_equal(&kg, &N));
	END_TEST

	TEST
		unit_t one_kg = MAKE_UNIT(1.0, U_KILOGRAM, 1);
		unit_t five_kg = MAKE_UNIT(5.0, U_KILOGRAM, 1);

		CHECK(!ul_equal(&one_kg, &five_kg));
		CHECK(ul_cmp(&one_kg, &five_kg) == UL_SAME_UNIT);

		unit_t five_sec = MAKE_UNIT(5.0, U_SECOND, 1);
		CHECK(ul_cmp(&five_kg, &five_sec) == UL_SAME_FACTOR);

		CHECK(ul_cmp(&one_kg, &five_sec) == UL_DIFFERENT);

		CHECK(ul_cmp(NULL, &one_kg) == UL_ERROR);
		CHECK(ul_cmp(&one_kg, NULL) == UL_ERROR);
		CHECK(ul_cmp(NULL, NULL) == UL_ERROR);

		/* UL_EQUAL result is tested by the previous test */
	END_TEST

	TEST
		unit_t a = MAKE_UNIT(1.0, U_KILOGRAM, 1);
		unit_t b;

		CHECK(ul_copy(&b, &a));
		FAIL_MSG("Error: %s", ul_error());

		CHECK(ul_equal(&b, &a));

		CHECK(!ul_copy(NULL, NULL));
		CHECK(!ul_copy(&a, NULL));
		CHECK(!ul_copy(NULL, &a));
	END_TEST

	TEST
		unit_t a = MAKE_UNIT(2.0, U_KILOGRAM, 1);
		unit_t b = MAKE_UNIT(3.0, U_SECOND, -2);

		unit_t res;
		CHECK(ul_copy(&res, &a));
		FAIL_MSG("Preparation failed: %s", ul_error());

		CHECK(ul_combine(&res, &b));
		FAIL_MSG("Error: %s", ul_error());

		unit_t correct = MAKE_UNIT(6.0, U_KILOGRAM, 1, U_SECOND, -2);
		CHECK(ul_equal(&res, &correct));
	END_TEST

	TEST
		unit_t test = MAKE_UNIT(1.0, U_KILOGRAM, 1);

		CHECK(ul_factor(&test) == 1.0);
		CHECK(ul_mult(&test, 5.0));
		CHECK(ul_factor(&test) == 5.0);
		CHECK(ul_mult(&test, 1/5.0));
		CHECK(ul_factor(&test) == 1.0);
		CHECK(ul_mult(&test, -1));
		CHECK(ul_factor(&test) == -1.0);
	END_TEST
END_TEST_SUITE()

TEST_SUITE(format)
	TEST
		extern void _ul_getnexp(ul_number n, ul_number *m, int *e);

		ul_number m;
		int       e;

		_ul_getnexp(1.0, &m, &e);
		CHECK(ncmp(m, 1.0) == 0);
		FAIL_MSG("m == %g", m);
		CHECK(e == 0);
		FAIL_MSG("e == %d", e);

		_ul_getnexp(-1.0, &m, &e);
		CHECK(ncmp(m, -1.0) == 0);
		FAIL_MSG("m == %g", m);
		CHECK(e == 0);
		FAIL_MSG("e == %d", e);

		_ul_getnexp(11.0, &m, &e);
		CHECK(ncmp(m, 1.1) == 0);
		FAIL_MSG("m == %g", m);
		CHECK(e == 1);
		FAIL_MSG("e == %d", e);;

		_ul_getnexp(9.81, &m, &e);
		CHECK(ncmp(m, 9.81) == 0);
		FAIL_MSG("m == %g", m);
		CHECK(e == 0);
		FAIL_MSG("e == %d", e);

		_ul_getnexp(-1234, &m, &e);
		CHECK(ncmp(m, -1.234) == 0);
		FAIL_MSG("m == %g", m);
		CHECK(e == 3);
		FAIL_MSG("e == %d", e);

		_ul_getnexp(10.0, &m, &e);
		CHECK(ncmp(m, 1.0) == 0);
		FAIL_MSG("m == %g", m);
		CHECK(e == 1);
		FAIL_MSG("e == %d", e);

		_ul_getnexp(0.01, &m, &e);
		CHECK(ncmp(m, 1.0) == 0);
		FAIL_MSG("m == %g", m);
		CHECK(e == -2);
		FAIL_MSG("e == %d", e);

		_ul_getnexp(0.99, &m, &e);
		CHECK(ncmp(m, 9.9) == 0);
		FAIL_MSG("m == %g", m);
		CHECK(e == -1);
		FAIL_MSG("e == %d", e);

		_ul_getnexp(10.01, &m, &e);
		CHECK(ncmp(m, 1.001) == 0);
		FAIL_MSG("m == %g", m);
		CHECK(e == 1);
		FAIL_MSG("e == %d", e);
	END_TEST

	TEST
		unit_t kg = MAKE_UNIT(1.0, U_KILOGRAM, 1);

		char buffer[128];
		CHECK(ul_snprint(buffer, 128, &kg, UL_FMT_PLAIN, 0));
		FAIL_MSG("Error: %s", ul_error());
		CHECK(strcmp(buffer, "1 kg") == 0);
		FAIL_MSG("buffer: '%s'", buffer);
		CHECK(ul_length(&kg, UL_FMT_PLAIN, 0) == strlen(buffer));
		FAIL_MSG("ul_length: %u", ul_length(&kg, UL_FMT_PLAIN, 0));

		kg.factor = 1.5;
		CHECK(ul_snprint(buffer, 128, &kg, UL_FMT_PLAIN, 0));
		FAIL_MSG("Error: %s", ul_error());
		CHECK(strcmp(buffer, "1.5 kg") == 0);
		FAIL_MSG("buffer: '%s'", buffer);
		CHECK(ul_length(&kg, UL_FMT_PLAIN, 0) == strlen(buffer));
		FAIL_MSG("ul_length: %u", ul_length(&kg, UL_FMT_PLAIN, 0));

		kg.factor = -1.0;
		CHECK(ul_snprint(buffer, 128, &kg, UL_FMT_PLAIN, 0));
		FAIL_MSG("Error: %s", ul_error());
		CHECK(strcmp(buffer, "-1 kg") == 0);
		FAIL_MSG("buffer: '%s'", buffer);
		CHECK(ul_length(&kg, UL_FMT_PLAIN, 0) == strlen(buffer));
		FAIL_MSG("ul_length: %u", ul_length(&kg, UL_FMT_PLAIN, 0));
	END_TEST

	TEST
		unit_t N = MAKE_UNIT(1.0, U_KILOGRAM, 1, U_SECOND, -2, U_METER, 1);

		char buffer[128];
		CHECK(ul_snprint(buffer, 128, &N, UL_FMT_PLAIN, 0));
		FAIL_MSG("Error: %s", ul_error());

		CHECK(strcmp(buffer, "1 m kg s^-2") == 0);
		FAIL_MSG("buffer: '%s'", buffer);

		CHECK(ul_length(&N, UL_FMT_PLAIN, 0) == strlen(buffer));
		FAIL_MSG("ul_length: %u", ul_length(&N, UL_FMT_PLAIN, 0));
	END_TEST

	TEST
		unit_t N = MAKE_UNIT(1.0, U_KILOGRAM, 1, U_SECOND, -2, U_METER, 1);

		char buffer[128];
		CHECK(ul_snprint(buffer, 128, &N, UL_FMT_LATEX_INLINE, 0));
		FAIL_MSG("Error: %s", ul_error());

		CHECK(strcmp(buffer, "$1 \\text{ m} \\text{ kg} \\text{ s}^{-2}$") == 0);
		FAIL_MSG("buffer: '%s'", buffer);

		CHECK(ul_length(&N, UL_FMT_LATEX_INLINE, 0) == strlen(buffer));
		FAIL_MSG("ul_length: %u", ul_length(&N, UL_FMT_LATEX_INLINE, 0));
	END_TEST

	TEST
		unit_t N = MAKE_UNIT(1.0, U_KILOGRAM, 1, U_SECOND, -2, U_METER, 1);

		char buffer[128];
		CHECK(ul_snprint(buffer, 128, &N, UL_FMT_LATEX_FRAC, 0));
		FAIL_MSG("Error: %s", ul_error());

		CHECK(strcmp(buffer, "$\\frac{1 \\text{ m} \\text{ kg}}{\\text{s}^{2}}$") == 0);
		FAIL_MSG("buffer: '%s'", buffer);

		CHECK(ul_length(&N, UL_FMT_LATEX_FRAC, 0) == strlen(buffer));
		FAIL_MSG("ul_length: %zu", ul_length(&N, UL_FMT_LATEX_FRAC, 0));
	END_TEST

	TEST
		unit_t zeroKg = MAKE_UNIT(0.0, U_KILOGRAM, 1);

		char buffer[128];
		CHECK(ul_snprint(buffer, 128, &zeroKg, UL_FMT_PLAIN, 0));
		FAIL_MSG("Error: %s", ul_error());
		CHECK(strcmp(buffer, "0 kg") == 0);
		FAIL_MSG("buffer: '%s'", buffer);
	END_TEST
END_TEST_SUITE()

TEST_SUITE(reduce)
	TEST
		CHECK(ul_parse_rule("N = 1 kg m s^-2"));

		unit_t N = MAKE_UNIT(1, U_KILOGRAM, 1, U_METER, 1, U_SECOND, -2);

		char buffer[128];
		CHECK(ul_snprint(buffer, 128, &N, UL_FMT_PLAIN, UL_FOP_REDUCE));
		CHECK(strcmp(buffer, "1 N") == 0);

		CHECK(ul_snprint(buffer, 128, &N, UL_FMT_LATEX_INLINE, UL_FOP_REDUCE));
		CHECK(strcmp(buffer, "$1 \\text{ N}$") == 0);

		CHECK(ul_snprint(buffer, 128, &N, UL_FMT_LATEX_FRAC, UL_FOP_REDUCE));
		CHECK(strcmp(buffer, "$1 \\text{ N}$") == 0);
	END_TEST
END_TEST_SUITE()

int main(void)
{
	ul_debugging(true);
	ul_debugout("test/utest-debug.log", false);
	if (!ul_init()) {
		printf("ul_init failed: %s", ul_error());
		return 1;
	}

	INIT_TEST();
	SET_LOGLVL(L_NORMAL);
	USE_AUTO_FAIL();

	RUN_SUITE(core);
	RUN_SUITE(parser);
	RUN_SUITE(format);
	RUN_SUITE(reduce);

	ul_quit();

	return TEST_RESULT;
}

#endif /*ndef GET_TEST_DEFS*/

//#################################################################################################

#ifdef GET_TEST_DEFS
#define PRINT(o, lvl, ...) do { if ((o)->loglvl >= lvl) printf(__VA_ARGS__); } while (0)

#define FATAL(expr) \
	do { \
		if (expr) { \
			PRINT(_o, L_RESULT, "[%s%s-%d] Fatal: %s\n", _name, _group_name, _id, #expr); \
			exit(1); \
		} \
	} while (0)

#define CHECK(expr) \
	do { \
	  int _this = ++_cid; \
		if (!(expr)) { \
			_err++; _fail++; \
			PRINT(_o, L_NORMAL, "[%s%s-%d-%d] (%d) Fail: '%s'\n", _name, _group_name, _id, _this, __LINE__, #expr); \
			_last = false;\
			if (_o->autofail) _o->autofail(_name, _id, _this, #expr); \
		} \
		else { \
			PRINT(_o, L_VERBOSE, "[%s%s-%d-%d] Pass: '%s'\n", _name, _group_name, _id, _this, #expr); \
			_last = true; \
		} \
	} while (0)

#define AUTO_FAIL \
	static inline void _auto_fail(const char *Suite, int Test, int Check, const char *Expr) {

#define END_AUTO_FAIL \
	}

#define FAIL_MSG(msg, ...) \
	do {if (!_last) PRINT(_o,L_NORMAL,msg"\n", ##__VA_ARGS__); } while (0)

#define PASS_MSG(msg, ...) \
	do {if (_last) PRINT(_o,L_VERBOSE,msg"\n", ##__VA_ARGS__); } while (0)

#define INFO(fmt, ...) \
	do { printf("* " fmt "\n", ##__VA_ARGS__); } while (0)

// TEST SUITE
#define TEST_SUITE(name)          \
	int _test_##name(_tops *_o) {   \
		const char *_name = #name;    \
		const char *_group_name = ""; \
		int _fail = 0;                \
		int _test_id = 0;             \

#define END_TEST_SUITE() \
	return _fail; }

#define GROUP(name) \
	do { const char *_group_name = "-" name; int _test_id = 0;

#define END_GROUP() \
	} while (0);

#define SKIP_GROUP()	PRINT(_o, L_NORMAL, "[%s%s] skipped.\n", _name, _group_name); break;

// SINGLE TEST
#define TEST \
	{ int _id = ++_test_id; int _err = 0; int _cid = 0; bool _last = true; \

#define END_TEST \
		if (_err > 0) { \
			PRINT(_o, L_NORMAL, "[%s%s-%d] failed with %d error%s.\n", _name, _group_name, _id, _err, _err > 1 ? "s" : ""); \
		} \
		else { \
			PRINT(_o, L_NORMAL, "[%s%s-%d] passed.\n", _name, _group_name, _id); \
		} \
	}

// OTHER
typedef struct
{
	int loglvl;
	void (*autofail)(const char*,int,int,const char*);
} _tops;

enum
{
	L_QUIET   = 0,
	L_RESULT  = 1,
	L_NORMAL  = 2,
	L_VERBOSE = 3,
	L_ALL     = 4,
};

static inline int _run_suite(int (*suite)(_tops*), const char *name, _tops *o)
{
	int num_errs = suite(o);

	if (num_errs == 0) {
		PRINT(o, L_RESULT, "[%s] passed.\n", name);
	}
	else {
		PRINT(o, L_RESULT, "[%s] failed with %d error%s.\n", name, num_errs, num_errs > 1 ? "s" : "");
	}
	return num_errs;
}

#define INIT_TEST() \
	_tops _ops = {L_RESULT,NULL}; int _tres = 0;

#define SET_LOGLVL(lvl) \
	_ops.loglvl = (lvl);

#define USE_AUTO_FAIL() \
	_ops.autofail = _auto_fail;

#define RUN_SUITE(name) \
	_tres += _run_suite(_test_##name, #name, &_ops)

#define TEST_RESULT _tres

#endif /* GET_TEST_DEFS*/
