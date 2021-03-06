#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intern.h"
#include "unitlib.h"

// My string.h is missing strdup so place it here.
char *strdup(const char *s1);

// A unit conversion rule
typedef struct rule
{
	const char *symbol;
	unit_t unit;
	bool   force;
	struct rule *next;
} rule_t;

// A unit prefix (like mili)
typedef struct prefix
{
	char symbol;
	ul_number value;
	struct prefix *next;
} prefix_t;

// A list of all rules
static rule_t *rules = NULL;
// The base rules
static rule_t base_rules[NUM_BASE_UNITS];
// A list of all prefixes
static prefix_t *prefixes = NULL;

// Symbolic definition for the first dynamic allocated rule
// valid after _ul_init_parser() ist called
#define dynamic_rules (base_rules[NUM_BASE_UNITS-1].next)

enum {
	STACK_SIZE = 16,     // Size of the parser state stack
	MAX_SYM_SIZE = 128,   // Maximal size of a symbol
	MAX_ITEM_SIZE = 1024, // Maximal size of a composed item
};

// State in ()
struct substate
{
	unit_t unit;
	bool   sqrt;
	int    sign;
};

// The state of an ongoing parse
struct parser_state
{
	// state stack and current position
	size_t spos;
	struct substate stack[STACK_SIZE];

	bool brkt;  // true if the next item has to be an opening bracket
	char wasop; // true if the last item was an operator ('*' or '/')
	bool nextsqrt; // true if the next substate is in sqrt
};
#define CURRENT(what,state) (state)->stack[(state)->spos].what

// Result of a handle_* call
enum result {
	RS_ERROR,    // Something was wrong
	RS_HANDLED,  // The job is done
	RS_NOT_MINE, // Not my business
};
#define HANDLE_RESULT(marg_rs) \
	do { \
		enum result macro_rs = marg_rs; \
		if (macro_rs == RS_ERROR) { \
			return false; \
		} \
		if (macro_rs == RS_HANDLED) { \
			return true; \
		} \
		assert(macro_rs == RS_NOT_MINE); \
	} while (0);

// Returns the last rule in the list
static rule_t *last_rule(void)
{
	rule_t *cur = rules;
	while (cur) {
		if (!cur->next)
			return cur;
		cur = cur->next;
	}
	// rules cannot be NULL
	assert(false);
	return NULL;
}

// Returns the rule to a symbol
static rule_t *get_rule(const char *sym)
{
	assert(sym);
	for (rule_t *cur = rules; cur; cur = cur->next) {
		if (strcmp(cur->symbol, sym) == 0)
			return cur;
	}
	return NULL;
}

// Returns the last prefix in the list
static prefix_t *last_prefix(void)
{
	prefix_t *cur = prefixes;
	while (cur) {
		if (!cur->next)
			return cur;
		cur = cur->next;
	}
	return NULL;
}

// Returns the prefix definition to a character
static prefix_t *get_prefix(char sym)
{
	for (prefix_t *cur = prefixes; cur; cur = cur->next) {
		if (cur->symbol == sym)
			return cur;
	}
	return NULL;
}

// Skips all spaces at the beginning of the string
static size_t skipspace(const char *text, size_t start)
{
	assert(text);
	size_t i = start;
	while (text[i] && isspace(text[i]))
		i++;
	return i;
}

// Returns the position of the next space in the string
static size_t nextspace(const char *text, size_t start)
{
	assert(text);
	size_t i = start;
	while (text[i] && !isspace(text[i]))
		i++;
	return i;
}

static bool splitchars[256] = {
	['*'] = true,
	['/'] = true,
	['('] = true,
	[')'] = true,
};
static inline bool issplit(char c)
{
	return splitchars[(int)c];
}

// Returns the position of the next split character or space in the string
static size_t nextsplit(const char *text, size_t start)
{
	assert(text);
	size_t i = start;
	while (text[i] && !(isspace(text[i]) || issplit(text[i])))
		i++;
	return i;
}

static void init_substate(struct substate *sst)
{
	sst->sign = 1;
	init_unit(&sst->unit);
	sst->sqrt = false;
}

static bool push_unit(struct parser_state *state)
{
	state->spos++;
	debug("Push: %u -> %u", state->spos-1, state->spos);
	if (state->spos >= STACK_SIZE) {
		ERROR("Maximal nesting level exceeded.");
		return false;
	}

	init_substate(&state->stack[state->spos]);

	if (state->nextsqrt) {
		CURRENT(sqrt,state) = true;
		state->nextsqrt = false;
	}
	return true;
}

static bool pop_unit(struct parser_state *state, int exp)
{
	if (state->spos == 0) {
		ERROR("Internal error: Stack missmatch!");
		return false;
	}
	bool sqrt = CURRENT(sqrt, state);
	CURRENT(sqrt, state) = false;

	unit_t *top = &CURRENT(unit, state);
	state->spos--;

	if (sqrt && !ul_sqrt(top))
			return false;

	exp *= CURRENT(sign,state);

	add_unit(&CURRENT(unit,state), top, exp);
	return true;
}

static enum result sym_and_exp(const char *str, char *sym, int *exp)
{
	assert(str); assert(sym); assert(exp);

	size_t symend = 0;

	while (str[symend] && str[symend] != '^')
		symend++;

	if (symend >= MAX_SYM_SIZE) {
		ERROR("Symbol to long");
		return RS_ERROR;
	}
	strncpy(sym, str, symend);
	sym[symend] = '\0';

	*exp = 1;
	enum result rs = RS_NOT_MINE;

	if (str[symend]) {
		// The '^' should not be the last value of the string
		if (!str[symend+1]) {
			ERROR("Missing exponent after '^' while parsing '%s'", str);
			return RS_ERROR;
		}

		// Parse the exponent
		char *endptr = NULL;
		*exp = strtol(str+symend+1, &endptr, 10);

		// the whole exp string was valid only if *endptr is '\0'
		if (endptr && *endptr) {
			ERROR("Invalid exponent at char '%c' while parsing '%s'", *endptr, str);
			return RS_ERROR;
		}
		rs = RS_HANDLED;
	}
	return rs;
}

static enum result handle_bracket_end(const char *str, struct parser_state *state)
{
	char sym[MAX_SYM_SIZE];
	int exp = 1;

	if (sym_and_exp(str, sym, &exp) == RS_ERROR) {
		return RS_ERROR;
	}

	if (!pop_unit(state, exp))
		return RS_ERROR;
	return RS_HANDLED;
}

static enum result handle_special(const char *str, struct parser_state *state)
{
	assert(str); assert(state);

	debug("handle_special(%s)", str);

	size_t len = strlen(str);

	if (state->brkt && (len > 1 || str[0] != '(')) {
		ERROR("Opening bracket expected after sqrt!");
		return false;
	}

	if (len == 1 || str[0] == ')') {
		switch (str[0]) {

		case '/':
			CURRENT(sign, state) *= -1;
			// big bad fallthrough
			// * has no effect, and so the error handling
			// code is not doubled
		case '*':
			if (state->wasop) {
				ERROR("Cannot have %c right after %c.", str[0], state->wasop);
				return RS_ERROR;
			}
			state->wasop = str[0];
			return RS_HANDLED;

		case '(':
			state->wasop = '\0';
			state->brkt = false;
			if (!push_unit(state))
				return RS_ERROR;
			return RS_HANDLED;

		case ')':
			state->wasop = '\0';
			return handle_bracket_end(str, state);
		}
	}
	state->wasop = '\0';

	if (strcmp(str, "sqrt") == 0) {
		debug("Found sqrt");
		if (state->spos + 1 < STACK_SIZE)
			state->nextsqrt = true;
		state->brkt = true;
		return RS_HANDLED;
	}

	debug("not special!");
	return RS_NOT_MINE;
}

static enum result handle_factor(const char *str, struct parser_state *state)
{
	assert(str); assert(state);
	char *endptr;
	ul_number f = _strton(str, &endptr);
	if (endptr && *endptr) {
		return RS_NOT_MINE;
	}
	debug("'%s' is a factor", str);

	CURRENT(unit,state).factor *= _pown(f, CURRENT(sign,state));

	return RS_HANDLED;
}

static bool unit_and_prefix(const char *sym, unit_t **unit, ul_number *prefix)
{
	rule_t *rule = get_rule(sym);
	if (rule) {
		*unit = &rule->unit;
		*prefix = 1.0;
		return true;
	}

	char p = sym[0];
	debug("Got prefix: %c", p);
	prefix_t *pref = get_prefix(p);
	if (!pref) {
		ERROR("Unknown symbol: '%s'", sym);
		return false;
	}

	rule = get_rule(sym + 1);
	if (!rule) {
		ERROR("Unknown symbol: '%s' with prefix %c", sym + 1, p);
		return false;
	}

	*unit = &rule->unit;
	*prefix = pref->value;
	return true;
}

static enum result handle_unit(const char *str, struct parser_state *state)
{
	assert(str); assert(state);
	debug("Parse item: '%s'", str);

	// Split symbol and exponent
	char symbol[MAX_SYM_SIZE];
	int exp = 1;

	if (sym_and_exp(str, symbol, &exp) == RS_ERROR) {
		return RS_ERROR;
	}
	exp *= CURRENT(sign, state);

	unit_t *rule;
	ul_number prefix;
	if (!unit_and_prefix(symbol, &rule, &prefix))
		return RS_ERROR;

	// And add the definitions
	add_unit(&CURRENT(unit,state), rule,  exp);
	CURRENT(unit,state).factor *= _pown(prefix, exp);

	return RS_HANDLED;
}

static bool handle_item(const char *item, struct parser_state *state)
{
	HANDLE_RESULT(handle_special(item, state)); // special has to be the first one!
	HANDLE_RESULT(handle_factor(item, state));
	HANDLE_RESULT(handle_unit(item, state));
	ERROR("Unknown item type for item '%s'", item);
	return false;
}

UL_API bool ul_parse(const char *str, unit_t *unit)
{
	if (!str || !unit) {
		ERROR("Invalid paramters");
		return false;
	}
	debug("Parse unit: '%s'", str);

	struct parser_state state = {
		.spos  = 0,
		.brkt  = false,
		.wasop = '\0',
		.nextsqrt = false,
	};
	init_substate(&state.stack[0]);

	size_t len = strlen(str);
	size_t start = 0;
	do {
		char this_item[MAX_ITEM_SIZE ];

		// Skip leading whitespaces
		start = skipspace(str, start);
		// And find the next whitespace
		size_t end = nextsplit(str, start);

		// HACK
		if ((str[start] == ')') && (str[start+1] == '^')) {
			debug("Exp hack!");
			end = nextsplit(str, start+1);
		}

		debug("Start: %d", start);
		debug("End:   %d", end);

		if (end == start) {
			if (end == len) // end of string
				break;
			end++; // this one is a single splitchar
		}

		// sanity check
		if ((end - start) > MAX_ITEM_SIZE ) {
			ERROR("Item too long");
			return false;
		}

		// copy the item out of the string
		strncpy(this_item, str+start, end-start);
		this_item[end-start] = '\0';

		debug("Item is '%s'", this_item);

		// and handle it
		if (!handle_item(this_item, &state))
			return false;

		start = end;
	} while (start < len);

	if (state.spos != 0) {
		ERROR("Bracket missmatch");
		return false;
	}

	copy_unit(&state.stack[0].unit, unit);

	return true;
}

static bool add_rule(const char *symbol, const unit_t *unit, bool force)
{
	assert(symbol);	assert(unit);
	rule_t *rule = malloc(sizeof(*rule));
	if (!rule) {
		ERROR("Failed to allocate memory");
		return false;
	}
	rule->next = NULL;

	rule->symbol = symbol;
	rule->force  = force;

	copy_unit(unit, &rule->unit);

	rule_t *last = last_rule();
	last->next = rule;

	return true;
}

static bool add_prefix(char sym, ul_number n)
{
	prefix_t *pref = malloc(sizeof(*pref));
	if (!pref) {
		ERROR("Failed to allocate %d bytes", sizeof(*pref));
		return false;
	}

	pref->symbol = sym;
	pref->value  = n;
	pref->next = NULL;

	prefix_t *last = last_prefix();
	if (last)
		last->next = pref;
	else
		prefixes = pref;
	return true;
}

static bool rm_rule(rule_t *rule)
{
	assert(rule);
	if (rule->force) {
		ERROR("Cannot remove forced rule");
		return false;
	}

	rule_t *cur = dynamic_rules; // base rules cannot be removed
	rule_t *prev = &base_rules[NUM_BASE_UNITS-1];

	while (cur && cur != rule) {
		prev = cur;
		cur = cur->next;
	}

	if (cur != rule) {
		ERROR("Rule not found.");
		return false;
	}

	prev->next = rule->next;
	return true;
}

static bool valid_symbol(const char *sym)
{
	assert(sym);
	while (*sym) {
		if (!isalpha(*sym))
			return false;
		sym++;
	}
	return true;
}

static char *get_symbol(const char *rule, size_t splitpos, bool *force)
{
	assert(rule); assert(force);
	size_t skip   = skipspace(rule, 0);
	size_t symend = nextspace(rule, skip);
	if (symend > splitpos)
		symend = splitpos;

	if (skipspace(rule,symend) != splitpos) {
		// rule was something like "a b = kg"
		ERROR("Invalid symbol, whitespaces are not allowed.");
		return NULL;
	}

	if ((symend-skip) > MAX_SYM_SIZE) {
		ERROR("Symbol to long");
		return NULL;
	}
	if ((symend-skip) == 0) {
		ERROR("Empty symbols are not allowed.");
		return NULL;
	}

	if (rule[skip] == '!') {
		debug("Forced rule.");
		*force = true;
		skip++;
	}
	else {
		*force = false;
	}

	debug("Allocate %d bytes", symend-skip + 1);
	char *symbol = malloc(symend-skip + 1);
	if (!symbol) {
		ERROR("Failed to allocate memory");
		return NULL;
	}

	strncpy(symbol, rule + skip, symend-skip);
	symbol[symend-skip] = '\0';
	debug("Symbol is '%s'", symbol);

	return symbol;
}

// parses a string like "symbol = def"
UL_API bool ul_parse_rule(const char *rule)
{
	if (!rule) {
		ERROR("Invalid parameter");
		return false;
	}

	// split symbol and definition
	size_t len = strlen(rule);
	size_t splitpos = 0;

	debug("Parsing rule '%s'", rule);

	for (size_t i=0; i < len; ++i) {
		if (rule[i] == '=') {
			debug("Split at %d", i);
			splitpos = i;
			break;
		}
	}
	if (!splitpos) {
		ERROR("Missing '=' in rule definition '%s'", rule);
		return false;
	}

	// Get the symbol
	bool force = false;
	char *symbol = get_symbol(rule, splitpos, &force);
	if (!symbol)
		return false;

	if (!valid_symbol(symbol)) {
		ERROR("Symbol '%s' is invalid.", symbol);
		free(symbol);
		return false;
	}

	rule_t *old_rule = NULL;
	if ((old_rule = get_rule(symbol)) != NULL) {
		if (old_rule->force || !force) {
			ERROR("You may not redefine '%s'", symbol);
			free(symbol);
			return false;
		}
		// remove the old rule, so it cannot be used in the definition
		// of the new one, so something like "!R = R" is not possible
		if (force) {
			if (!rm_rule(old_rule)) {
				free(symbol);
				return false;
			}
		}
	}

	rule = rule + splitpos + 1; // ommiting the '='
	debug("Rest definition is '%s'", rule);

	unit_t unit;
	if (!ul_parse(rule, &unit)) {
		free(symbol);
		return false;
	}

	return add_rule(symbol, &unit, force);
}

UL_API bool ul_load_rules(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		ERROR("Failed to open file '%s'", path);
		return false;
	}

	bool ok = true;
	char line[1024];
	while (fgets(line, 1024, f)) {
		size_t skip = skipspace(line, 0);
		if (!line[skip] || line[skip] == '#')
			continue; // empty line or comment
		ok = ul_parse_rule(line);
		if (!ok)
			break;
	}
	fclose(f);
	return ok;
}

UL_LINKAGE const char *_ul_reduce(const unit_t *unit)
{
	for (rule_t *cur = rules; cur; cur = cur->next) {
		if (ul_cmp(&cur->unit, unit) & UL_SAME_UNIT)
			return cur->symbol;
	}
	return NULL;
}

static bool kilogram_hack(void)
{
	// stupid inconsistend SI system...
	unit_t gram = {
		{[U_KILOGRAM] = 1},
		1e-3,
	};
	if (!add_rule(strdup("g"), &gram, true)) // strdup because add_rule expects malloc'd memory (it gets free'd at ul_quit)
		return false;
	return true;
}

static void free_rules(void)
{
	rule_t *cur = dynamic_rules;
	while (cur) {
		rule_t *next = cur->next;
		free((char*)cur->symbol);
		free(cur);
		cur = next;
	}
	dynamic_rules = NULL;
}

static void free_prefixes(void)
{
	prefix_t *pref = prefixes;
	while (pref) {
		prefix_t *next = pref->next;
		free(pref);
		pref = next;
	}
	prefixes = NULL;
}

UL_API bool ul_reset_rules(void)
{
	free_rules();
	kilogram_hack();
	return true;
}

static bool init_prefixes(void)
{
	debug("Initializing prefixes");
	if (!add_prefix('Y', 1e24))  return false;
	if (!add_prefix('Z', 1e21))  return false; // zetta
	if (!add_prefix('E', 1e18))  return false; // exa
	if (!add_prefix('P', 1e15))  return false; // peta
	if (!add_prefix('T', 1e12))  return false; // tera
	if (!add_prefix('G', 1e9))   return false; // giga
	if (!add_prefix('M', 1e6))   return false; // mega
	if (!add_prefix('k', 1e3))   return false; // kilo
	if (!add_prefix('h', 1e2))   return false; // hecto
	// missing: da - deca
	if (!add_prefix('d', 1e-1))  return false; // deci
	if (!add_prefix('c', 1e-2))  return false; // centi
	if (!add_prefix('m', 1e-3))  return false; // milli
	if (!add_prefix('u', 1e-6))  return false; // micro
	if (!add_prefix('n', 1e-9))  return false; // nano
	if (!add_prefix('p', 1e-12)) return false; // pico
	if (!add_prefix('f', 1e-15)) return false; // femto
	if (!add_prefix('a', 1e-18)) return false; // atto
	if (!add_prefix('z', 1e-21)) return false; // zepto
	if (!add_prefix('y', 1e-24)) return false; // yocto

	debug("Prefixes initialized!");
	return true;
}

UL_LINKAGE bool _ul_init_parser(void)
{
	debug("Initializing parser");
	for (int i=0; i < NUM_BASE_UNITS; ++i) {
		debug("Base rule: %d", i);
		base_rules[i].symbol = _ul_symbols[i];

		init_unit(&base_rules[i].unit);
		base_rules[i].force = true;
		base_rules[i].unit.exps[i] = 1;

		base_rules[i].next = &base_rules[i+1];
	}
	dynamic_rules = NULL;
	rules = base_rules;
	debug("Base rules initialized");

	if (!kilogram_hack())
		return false;

	if (!init_prefixes())
		return false;

	debug("Parser initalized!");
	return true;
}

UL_LINKAGE void _ul_free_rules(void)
{
	free_rules();
	free_prefixes();
}