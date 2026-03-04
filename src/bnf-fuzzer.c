#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

typedef struct {
	char *bytes;
	size_t len;
	size_t capacity;
} string_t;

typedef struct {
	const char *bytes;
	size_t len;
} string_view_t;

#define MAX_ARGS 256 

typedef struct {
	string_view_t program_name;
	string_view_t args[MAX_ARGS];
	int args_len;
} command_t;

typedef struct arena_t {
	uint8_t *data;
	size_t len;
	size_t capacity;
	struct arena_t *next;
} arena_t;

string_view_t string_view_from_cstr(const char *);
string_view_t string_view_from_string(string_t *str);
int32_t string_view_to_cstr(arena_t *arena, string_view_t *sv, char **out);
void string_view_print(string_view_t *sv);

int32_t cli_run(command_t *cmd);
char *cli_str_error(int32_t);

#define ERR_ARENA_OUT_OF_MEMORY        100
#define ERR_FAILED_TO_OPEN_FILE        101
#define ERR_FAILED_TO_SEEK_FILE        102
#define ERR_FAILED_TO_READ_FILE        103
#define ERR_INVALID_TOKEN              104
#define ERR_EXPECTED_IDENTIFIER        105
#define ERR_EXPECTED_RULE_DEFINITION   106
#define ERR_UNEXPECTED_NEWLINE         107
#define ERR_TOO_BIG_CONDITIONAL_RULE   108
#define ERR_INVALID_TOKEN_TYPE_IN_RULE 109
#define ERR_INVALID_RULE_DEFINITION    110
#define ERR_TOO_MANY_RULES_IN_FILE     111
#define ERR_BNF_IS_RECURSIVE           112

int32_t arena_new(arena_t *arena, size_t capacity);
void arena_free(arena_t *arena);
void *arena_alloc(arena_t *arena, size_t len);

int32_t io_read_file_path(arena_t *arena, string_view_t *path, string_t *out);

typedef struct {
	string_view_t input;
	uint64_t position;
	uint64_t read_position;
	char ch;
} lexer_t;

typedef enum {
	TOKEN_TYPE_IDENTIFIER,
	TOKEN_TYPE_EQUAL_DEFINTION,
	TOKEN_TYPE_NUMERIC_ATOM,
	TOKEN_TYPE_OR,
	TOKEN_TYPE_NEWLINE,
	TOKEN_TYPE_EOF,
} token_type_t;

typedef struct {
	token_type_t type;
	string_view_t value;
} token_t;

int32_t lexer_next_token(lexer_t *lexer, token_t *token);
lexer_t lexer_new(string_view_t sv);

#define MAX_GRAMMAR_UNIONS 10

typedef enum {
	NON_ATOM = 0,
	NUMERIC_ATOM,
} grammar_value_type_t;

typedef struct {
	grammar_value_type_t type;
	union {
		string_view_t non_atom;
		int32_t numeric_atom;
	} as;
} grammar_value_t;

typedef struct {
	grammar_value_t values[MAX_GRAMMAR_UNIONS];
	size_t len;
	uint8_t has_no_terminal;
} grammar_sequence_t;

typedef struct grammar_rule_t {
	string_view_t name;
	grammar_sequence_t alternatives[MAX_GRAMMAR_UNIONS];
	size_t alternatives_len;
} grammar_rule_t;

#define MAX_RULES_IN_FILE 1024

typedef struct {
	grammar_rule_t rules[MAX_RULES_IN_FILE];
	size_t len;
} grammar_rules_list_t;

void print_grammar_rules_list(grammar_rules_list_t *list);

typedef struct {
	arena_t *arena;
	lexer_t lexer;

	token_t curr_token;
	token_t peek_token;
} parser_t;

int32_t parser_new_parser(parser_t *parser, lexer_t lexer, arena_t *arena);
int32_t parser_parse(parser_t *parser, grammar_rules_list_t *out);

uint8_t all_rules_lead_to_atoms(grammar_rules_list_t *rules);

void fuzz_grammar(grammar_rules_list_t *rules);

// TODO: play with different weight distributions
void __compute_weight_with_terminal_bias(grammar_rule_t *rule, double *weights, size_t len, int depth);
size_t weighted_random_select(double *weights, grammar_rule_t *rule);
void expand_rule(string_view_t rule_name, grammar_rules_list_t *list, int current_depth);

int main(int argc, const char **argv)
{
	srand(time(NULL));
	command_t cmd = {0};
	if (argc == 0)
	{
		fprintf(stderr, "[ERROR]: for some reason your argc is 0, fuckoff\n");
		return 69;
	}

	if (argc == 1)
	{
		cmd.program_name = string_view_from_cstr(argv[0]);
	}

	if ((argc-1) >= MAX_ARGS)
	{
		fprintf(stderr, "[ERROR]: you are sending too much shit, max is %d, got %d\n", MAX_ARGS, (argc-1));
		return 420;
	}

	for (int32_t i = 1; i < argc; ++i)
	{
		cmd.args[cmd.args_len++] = string_view_from_cstr(argv[i]);
	}

	int32_t err_code;
	if ((err_code = cli_run(&cmd)) != 0)
	{
		fprintf(stderr, "[ERROR]: Failed to fuzz the grammar, error: %s\n", cli_str_error(err_code));
		return err_code;
	}

	return 0;
}

#define Kb(b) (b) * 1024
#define Mb(b) (b) * 1024 * 1024

int32_t cli_run(command_t *cmd)
{
	int32_t err_code = 0;

	string_t str = {0};
	arena_t arena = {0};
	
	if ((err_code = arena_new(&arena, Kb(10))) != 0)
	{
		return err_code;
	}

	string_view_t *file_path = &cmd->args[0];

	if ((err_code = io_read_file_path(&arena, file_path, &str)) != 0)
	{
		return err_code;
	}

	string_view_t fsv = string_view_from_string(&str);
	lexer_t lexer = lexer_new(fsv);

	parser_t parser = {0};

	if ((err_code = parser_new_parser(&parser, lexer, &arena)) != 0)
	{
		return err_code;
	}

	// TODO: Think if hashmap would be better, we are going to iterate over this quite a lot
	//       but tbh I am not implementing a hashmap now.
	grammar_rules_list_t grammar_rules = {0};

	if ((err_code = parser_parse(&parser, &grammar_rules)) != 0)
	{
		return err_code;
	}

	if (!all_rules_lead_to_atoms(&grammar_rules))
	{
		return ERR_BNF_IS_RECURSIVE;
	}

	fuzz_grammar(&grammar_rules);

	arena_free(&arena);
	
	return 0;
}

int32_t arena_new(arena_t *arena, size_t capacity)
{
	arena->len = 0;
	arena->capacity = capacity;
	arena->data = malloc(sizeof(uint8_t) * capacity);
	if (arena->data == NULL)
	{
		return ERR_ARENA_OUT_OF_MEMORY;
	}

	return 0;
}

void arena_free(arena_t *arena)
{
	free(arena->data);
	arena_t *aux = arena->next;
	arena_t *prev = NULL;
	while (aux != NULL)
	{
		prev = aux;
		aux = aux->next;
		free(prev->data);
		free(prev);
	}
}

char *cli_str_error(int32_t err_code)
{
	switch (err_code)
	{
	case ERR_ARENA_OUT_OF_MEMORY:
	{
		return "ran out of memory for allocating arena";
	}; break;
	case ERR_FAILED_TO_OPEN_FILE:
	{
		return "could not open file";
	}; break;
	case ERR_FAILED_TO_SEEK_FILE:
	{
		return "could not seek file";
	}; break;
	case ERR_FAILED_TO_READ_FILE:
	{
		return "could not read file";
	}; break;
	case ERR_INVALID_TOKEN:
	{
		return "unknown token value";
	}; break;
	case ERR_EXPECTED_IDENTIFIER:
	{
		return "invalid token, expected identifier";
	}; break;
	case ERR_EXPECTED_RULE_DEFINITION:
	{
		return "invalid token, expected ':='";
	}; break;
	case ERR_UNEXPECTED_NEWLINE:
	{
		return "it is impossible to declare an empty rule";
	}; break;
	case ERR_TOO_BIG_CONDITIONAL_RULE:
	{
		return "conditional rule is too big, max number of conditionals is 10";
	}; break;
	case ERR_INVALID_TOKEN_TYPE_IN_RULE:
	{
		return "rule value can either be a numerical atom or a non atom identifier";
	}; break;
	case ERR_INVALID_RULE_DEFINITION:
	{
		return "rule values must be either followed by a new line or by a | and another rule";
	}; break;
	case ERR_TOO_MANY_RULES_IN_FILE:
	{
		return "each file needs to have at most 1024 rules.";
	}; break;
	case ERR_BNF_IS_RECURSIVE:
	{
		return "every rule in a bnf has to lead to a non-terminal in the end.";
	}; break;
	}
	return NULL;
}

string_view_t string_view_from_cstr(const char *cstr)
{
	string_view_t sv = {0};
	sv.bytes = cstr;
	sv.len = strlen(cstr);
	return sv;
}

void string_view_print(string_view_t *sv)
{
	for (size_t i = 0; i < sv->len; ++i)
	{
		printf("%c", sv->bytes[i]);
	}

	printf("\n");
}

int32_t io_read_file_path(arena_t *arena, string_view_t *path, string_t *out)
{
	int32_t err_code = 0;
	char *file_path;

	if ((err_code = string_view_to_cstr(arena, path, &file_path)) != 0)
	{
		return err_code;
	}

	FILE *f = fopen(file_path, "r");
	if (f == NULL)
	{
		return ERR_FAILED_TO_OPEN_FILE;
	}

	int32_t fseek_res = fseek(f, 0, SEEK_END);
	if (fseek_res < 0)
	{
		fclose(f);
		return ERR_FAILED_TO_SEEK_FILE;
	}


	int32_t file_size = ftell(f);
	if (file_size < 0)
	{
		fclose(f);
		return ERR_FAILED_TO_SEEK_FILE;
	}

	rewind(f);

	char *file_content = arena_alloc(arena, sizeof(char) * file_size);

	size_t nbytes = fread(file_content, sizeof(char), file_size, f);
	if ((int32_t) nbytes != file_size)
	{
		fclose(f);
		return ERR_FAILED_TO_READ_FILE;
	}

	fclose(f);

	out->bytes = file_content;
	out->len = file_size;
	out->capacity = file_size;
	
	return 0;
}

int32_t string_view_to_cstr(arena_t *arena, string_view_t *sv, char **out)
{
	*out = arena_alloc(arena, (sv->len+1) * sizeof(char)); 
	if (!out)
	{
		return ERR_ARENA_OUT_OF_MEMORY;
	}
	for (size_t i = 0; i < sv->len; ++i)
	{
		(*out)[i] = sv->bytes[i];
	}

	(*out)[sv->len] = '\0';	

	return 0;
}

void *arena_alloc(arena_t *arena, size_t len)
{
	if (arena->len + len >= arena->capacity)
	{
		arena_t *aux = arena;
		while (aux->next != NULL)
		{
			aux = aux->next;
			if (aux->len + len < aux->capacity)
			{
				// we found one that fits.
				arena = aux;
				break;
			}
		}

		// If we didn't find a fitting arena, create a new one
		if (arena->len + len >= arena->capacity)
		{
			arena_t *n = malloc(sizeof(arena_t));
			int32_t err_code = arena_new(n, arena->capacity);
			if (err_code != 0)
			{
				return NULL;
			}

			// Link the new arena to the chain
			aux->next = n;
			arena = n;
		}
	}

	uint8_t *data = arena->data + arena->len;
	// TODO: Align, this is UB
	arena->len += len;
	return data;
}

string_view_t string_view_from_string(string_t *str)
{
	string_view_t sv = {0};
	sv.bytes = str->bytes;
	sv.len = str->len;
	return sv;
}

int32_t string_view_to_int_32_t(string_view_t sv)
{
    char tmp[12];
    memcpy(tmp, sv.bytes, sv.len);
    tmp[sv.len] = '\0';

    char *end;
    errno = 0;

    long val = strtol(tmp, &end, 10);

    if (errno == ERANGE || val < INT32_MIN || val > INT32_MAX)
        return -1;

    if (*end != '\0')
        return -1;

    return (int32_t)val;
}

char lexer_peek_char(lexer_t *l)
{
	if (l->read_position >= l->input.len)
	{
		return 0;
	}

	return l->input.bytes[l->read_position];
}

void lexer_read_char(lexer_t *l)
{
	if (l->read_position >= l->input.len)
	{
		l->ch = 0;
	}
	else
	{
		l->ch = l->input.bytes[l->read_position];
	}

	l->position = l->read_position;
	l->read_position++;
}

lexer_t lexer_new(string_view_t sv)
{
	lexer_t l = {0};
	l.input = sv;
	lexer_read_char(&l);
	return l;
}

#define IS_SPACE(c) (c) == ' ' || (c) == '\t' || (c) == '\r'
#define IS_DIGIT(c) '0' <= (c) && (c) <= '9'
#define IS_LETTER(c) ('a' <= (c) && (c) <= 'z') || ('A' <= (c) && (c) <= 'Z') || ((c) == '_')

void lexer_skip_whitespace(lexer_t *lexer)
{
	while (IS_SPACE(lexer->ch))
	{
		lexer_read_char(lexer);
	}
}

string_view_t lexer_read_identifier(lexer_t *lexer)
{
	const char *p = lexer->input.bytes + lexer->position;
	size_t len = 0;
	while (IS_LETTER(lexer->ch))
	{
		len++;
		lexer_read_char(lexer);
	}

	string_view_t sv = {.len=len, .bytes=p};
	return sv;
}

string_view_t lexer_read_number(lexer_t *lexer)
{
	const char *p = lexer->input.bytes + lexer->position;
	size_t len = 0;
	while (IS_DIGIT(lexer->ch))
	{
		len++;
		lexer_read_char(lexer);
	}

	string_view_t sv = {.len=len, .bytes=p};
	return sv;
}

int32_t lexer_next_token(lexer_t *lexer, token_t *token)
{
	lexer_skip_whitespace(lexer);
	
	switch (lexer->ch)
	{
		case ':':
		{
			if (lexer_peek_char(lexer) == '=')
			{
				token->type = TOKEN_TYPE_EQUAL_DEFINTION;
				lexer_read_char(lexer);
			}
		}; break;
		case '|':
		{
			token->type = TOKEN_TYPE_OR;
		}; break;
		case '\n':
		{
			token->type = TOKEN_TYPE_NEWLINE;
		}; break;
		case 0:
		{
			token->type = TOKEN_TYPE_EOF;
		}; break;
		default:
		{
			if (IS_LETTER(lexer->ch))
			{
				token->type = TOKEN_TYPE_IDENTIFIER;
				token->value = lexer_read_identifier(lexer);
				return 0;
			}
			else if (IS_DIGIT(lexer->ch))
			{
				token->type = TOKEN_TYPE_NUMERIC_ATOM;
				token->value = lexer_read_number(lexer);
				return 0;
			} else {
				lexer_read_char(lexer);
				return ERR_INVALID_TOKEN;
			}
		}; break;
	}

	lexer_read_char(lexer);

	return 0;
}

int32_t parser_next_token(parser_t *parser)
{
	parser->curr_token = parser->peek_token;
	int32_t err_code = 0;

	if ((err_code = lexer_next_token(&parser->lexer, &parser->peek_token)) != 0)
	{
		return err_code;
	}

	return 0;
}

int32_t parser_new_parser(parser_t *parser, lexer_t lexer, arena_t *arena)
{
	int32_t err_code;
	parser->lexer = lexer;
	parser->arena = arena;

	if ((err_code = parser_next_token(parser)) != 0)
	{
		return err_code;
	}

	if ((err_code = parser_next_token(parser)) != 0)
	{
		return err_code;
	}

	return 0;
}

uint8_t parser_current_token_is(parser_t *parser, token_type_t type)
{
	return parser->curr_token.type == type; 
}

uint8_t parser_peek_token_is(parser_t *parser, token_type_t type)
{
	return parser->peek_token.type == type; 
}

int32_t parser_parse_grammar_rule(parser_t *parser, grammar_rule_t *grammar_rule)
{
	if (!parser_current_token_is(parser, TOKEN_TYPE_IDENTIFIER))
	{
		return ERR_EXPECTED_IDENTIFIER;
	}

	grammar_rule->name = parser->curr_token.value;
	grammar_rule->alternatives_len = 0;

	int32_t err_code = 0;
	if ((err_code = parser_next_token(parser)) != 0)
	{
		return err_code;
	}

	if (!parser_current_token_is(parser, TOKEN_TYPE_EQUAL_DEFINTION))
	{
		return ERR_EXPECTED_RULE_DEFINITION;
	}

	if ((err_code = parser_next_token(parser)) != 0)
	{
		return err_code;
	}

	if (parser_current_token_is(parser, TOKEN_TYPE_NEWLINE))
	{
		return ERR_UNEXPECTED_NEWLINE;
	}

	grammar_sequence_t *current_sequence = &grammar_rule->alternatives[grammar_rule->alternatives_len++];
	current_sequence->len = 0;

	while (!parser_current_token_is(parser, TOKEN_TYPE_NEWLINE))
	{
		if (!parser_current_token_is(parser, TOKEN_TYPE_NUMERIC_ATOM) && !parser_current_token_is(parser, TOKEN_TYPE_IDENTIFIER))
		{
			return ERR_INVALID_TOKEN_TYPE_IN_RULE;
		}

		if (current_sequence->len >= MAX_GRAMMAR_UNIONS)
		{
			return ERR_TOO_BIG_CONDITIONAL_RULE;
		}

		grammar_value_t *value = &current_sequence->values[current_sequence->len];
		if (parser_current_token_is(parser, TOKEN_TYPE_NUMERIC_ATOM))
		{
			value->type = NUMERIC_ATOM;
			value->as.numeric_atom = string_view_to_int_32_t(parser->curr_token.value);
		}
		else
		{
			current_sequence->has_no_terminal = 1;
			value->type = NON_ATOM;
			value->as.non_atom = parser->curr_token.value;
		}

		current_sequence->len++;

		if ((err_code = parser_next_token(parser)) != 0)
		{
			return err_code;
		}

		if (parser_current_token_is(parser, TOKEN_TYPE_OR))
		{
			if (!parser_peek_token_is(parser, TOKEN_TYPE_NUMERIC_ATOM) && !parser_peek_token_is(parser, TOKEN_TYPE_IDENTIFIER))
			{
				return ERR_INVALID_TOKEN_TYPE_IN_RULE;
			}

			if ((err_code = parser_next_token(parser)) != 0)
			{
				return err_code;
			}

			// Start a new alternative
			if (grammar_rule->alternatives_len >= MAX_GRAMMAR_UNIONS)
			{
				return ERR_TOO_BIG_CONDITIONAL_RULE;
			}

			current_sequence = &grammar_rule->alternatives[grammar_rule->alternatives_len++];
			current_sequence->len = 0;
		}
	}

	if ((err_code = parser_next_token(parser)) != 0)
	{
		return err_code;
	}

	return 0;
}

int32_t grammar_rules_list_append(grammar_rules_list_t *list, grammar_rule_t *gr)
{
	if (list->len >= MAX_RULES_IN_FILE)
	{
		return ERR_TOO_MANY_RULES_IN_FILE;
	}

	list->rules[list->len++] = *gr;

	return 0;
}

int32_t parser_parse(parser_t *parser, grammar_rules_list_t *out)
{
	int32_t err_code = 0;
	out->len = 0;
	while (parser->curr_token.type != TOKEN_TYPE_EOF)
	{
		grammar_rule_t gr = {0};
		if ((err_code = parser_parse_grammar_rule(parser, &gr)) != 0)
		{
			return err_code;
		}

		if ((err_code = grammar_rules_list_append(out, &gr)) != 0)
		{
			return err_code;
		}
	}

	return 0;
}

void print_grammar_value(grammar_value_t *gv)
{
	switch (gv->type)
	{
		case NUMERIC_ATOM:
		{
			printf("%d", gv->as.numeric_atom);
		}; break;
		case NON_ATOM:
		{
			for (size_t i = 0; i < gv->as.non_atom.len; ++i)
			{
				printf("%c", gv->as.non_atom.bytes[i]);
			}
		}; break;
	}
}

void print_grammar_sequence(grammar_sequence_t *seq)
{
	for (size_t i = 0; i < seq->len; ++i)
	{
		print_grammar_value(&seq->values[i]);
		if (i < seq->len - 1)
		{
			printf(" ");
		}
	}
}

void print_grammar_rule(grammar_rule_t *gr)
{
	printf("----- grammar rule -----\nname: ");
	string_view_print(&gr->name);

	printf("alternatives:\n");
	for (size_t i = 0; i < gr->alternatives_len; ++i)
	{
		printf("  [%zu] ", i);
		print_grammar_sequence(&gr->alternatives[i]);
		printf("\n");
	}
}

void print_grammar_rules_list(grammar_rules_list_t *list)
{
	for (size_t i = 0; i < list->len; ++i)
	{
		grammar_rule_t *gt = &list->rules[i];
		print_grammar_rule(gt);
	}
}

size_t find_index_in_grammar(grammar_rules_list_t *list, string_view_t non_atom)
{
	for (size_t i = 0; i < list->len; ++i)
	{
		grammar_rule_t *gr = &list->rules[i];
		if (gr->name.len != non_atom.len)
		{
			continue;
		}

		if (gr->name.bytes == non_atom.bytes)
		{
			return i;
		}

		for (size_t j = 0; j < non_atom.len; ++j)
		{
			if (gr->name.bytes[j] != non_atom.bytes[j])
			{
				goto end;
			}
		}
		return i;
end:
		continue;
	}

	return MAX_RULES_IN_FILE;
} 

uint8_t all_rules_lead_to_atoms(grammar_rules_list_t *list)
{
	// A rule R is productive if there exists at least one alternative such that every symbol in that alternative is either: 
	// - a terminal (NUMERIC_ATOM), or
	// - a nonterminal that is itself productive.
	uint8_t productive[MAX_RULES_IN_FILE] = {0};

	uint8_t changed = 1;

	while (changed)
	{
		changed = 0;
		for (size_t i = 0; i < list->len; ++i)
		{
			if (productive[i])
			{
				continue;
			}

			grammar_rule_t *gr = &list->rules[i];

			for (size_t j = 0; j < gr->alternatives_len; j++)
			{
				grammar_sequence_t *sq = &gr->alternatives[j];
				uint8_t all_good = 1;
				for (size_t k = 0; k < sq->len; k++)
				{
					grammar_value_t *value = &sq->values[k];
					if (value->type == NUMERIC_ATOM)
					{
						continue;
					}

					size_t idx = find_index_in_grammar(list, value->as.non_atom);
					assert(idx < MAX_RULES_IN_FILE);
					if (!productive[idx])
					{
						all_good = 0;
						break;
					}
				}

				if (all_good)
				{
					productive[i] = 1;
					changed = 1;
					break;
				}
			}
		}
	}

	return productive[0];
}

void expand_value(grammar_rules_list_t *list, grammar_value_t *value, int depth)
{
	if (value->type == NUMERIC_ATOM)
	{
		printf("%d", value->as.numeric_atom);
	}
	else if (value->type == NON_ATOM)
	{
		expand_rule(value->as.non_atom, list, depth + 1);
	}
}

void expand_rule(string_view_t rule_name, grammar_rules_list_t *list, int current_depth)
{
	// TODO: Add max depth

	size_t idx = find_index_in_grammar(list, rule_name);
	assert(idx < MAX_RULES_IN_FILE);

	grammar_rule_t *rule = &list->rules[idx];

	double weights[MAX_GRAMMAR_UNIONS] = {0.0};
	__compute_weight_with_terminal_bias(rule, weights, MAX_GRAMMAR_UNIONS, current_depth);

	size_t selected_alt_idx = weighted_random_select(weights, rule);
	assert(selected_alt_idx < MAX_RULES_IN_FILE);

	grammar_sequence_t *seq = &rule->alternatives[selected_alt_idx];
	for (size_t i = 0; i < seq->len; ++i)
	{
		grammar_value_t *value = &seq->values[i];
		expand_value(list, value, current_depth);
	}
}

size_t weighted_random_select(double *weights, grammar_rule_t *rule)
{
	double total_weight = 0.0;
	for (size_t i = 0; i < rule->alternatives_len; i++)
	{
		total_weight += weights[i];
	}

	double random_value = ((double) rand() / RAND_MAX) * total_weight;
	
	double cumulative = 0.0;
	for (size_t i = 0; i < rule->alternatives_len; ++i)
	{
		cumulative += weights[i];
		if (random_value < cumulative)
		{
			return i;
		}
	}

	return rule->alternatives_len-1;
}

void __compute_weight_with_terminal_bias(grammar_rule_t *rule, double *weights, size_t len, int depth)
{
	assert(rule->alternatives_len <= len);

	for (size_t i = 0; i < rule->alternatives_len; ++i)
	{
		double base_weight = 1.0;
		grammar_sequence_t *seq = &rule->alternatives[i];

		if (seq->has_no_terminal)
		{
			weights[i] = (base_weight / (depth + 1));
		}
		else
		{
			weights[i] = base_weight * (depth + 1);
		}
	}
}

void fuzz_grammar(grammar_rules_list_t *list)
{
	expand_rule(list->rules[0].name, list, 0);
}

