#include "sema.h"
#include "ast.h"
#include "stretchy_buffer.h"
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

static Map declmap;
static Map typemap;

extern struct token_map keywords[];
static void map_push_scope(Map *m);
static void map_pop_scope(Map *m);
static void map_print(const Map *m);

static void fatal(const char *msg) {
	fprintf(stderr, "%s\n", msg);
	exit(EXIT_FAILURE);
}

static void error(const char *fmt, ...)
{
	va_list args;

	fprintf(stderr, "error: ");
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");

	printf("==== declmap ====\n");
	map_print(&declmap);
	printf("==== typemap ====\n");
	map_print(&typemap);

	exit(EXIT_FAILURE);
}

///
/// Name table API
///

static uint64_t strhash(const char *text, int len)
{
	uint64_t hash = 5381;

	for (int i = 0; i < len; i++) {
		int c = text[i];
		hash = ((hash << 5) + hash) + c; // hash * 33 + c
		len--;
	}

	return hash;
}

Name *push_name(NameTable *nt, char *s, size_t len)
{
	int i = strhash(s, len) % NAMETABLE_SIZE;
	Name *name = calloc(1, sizeof(Name));
	Name **p;

	name->text = calloc(len + 1, sizeof(char));
	strncpy(name->text, s, len);

	p = &nt->keys[i];
	name->next = *p;
	*p = name;

	return *p;
}

Name *get_name(NameTable *nt, char *s, size_t len)
{
	int index = strhash(s, len) % NAMETABLE_SIZE;

	for (Name *n = nt->keys[index]; n; n = n->next)
		if (strlen(n->text) == len && !strncmp(n->text, s, len))
			return n;

	return NULL;
}

Name *get_or_push_name(NameTable *nt, char *s, size_t len)
{
	Name *name = get_name(nt, s, len);

	if (!name)
		name = push_name(nt, s, len);

	return name;
}

static Type *make_type(Name *name, Type *canon_type)
{
	Type *type = calloc(1, sizeof(Type));

	type->name = name;
	type->canon_type = canon_type;

	return type;
}

static Decl *make_decl(Name *name, Type *type)
{
	Decl *decl = calloc(1, sizeof(Decl));

	decl->name = name;
	decl->type = type;

	return decl;
}

static void free_decl(Decl *decl)
{
	free(decl);
}

///
/// Symbol map API
///

static uint64_t ptrhash(const void *p) {
	uint64_t x = (uint64_t)p;

	x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
	x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
	x = x ^ (x >> 31);

	return x;
}

static Symbol *make_symbol(Map *m, Name *name, void *value) {
	Symbol *s;

	s = calloc(1, sizeof(Symbol));
	if (!s)
		fatal("out of memory");
	s->name = name;
	s->scope = m->n_scope - 1;
	s->value = value;

	return s;
}

static void free_symbol(Symbol *s) {
	free(s->value);
	free(s);
}

static void map_init(Map *m)
{
	memset(m, 0, sizeof(Map));
	map_push_scope(m);
}

static void map_destroy(Map *m)
{
	for (int i = 0; i < m->n_scope; i++)
		map_pop_scope(m);
	sb_free(m->scopes);
}

static Symbol *map_find(Map *m, Name *name) {
	int index = ptrhash(name) % HASHTABLE_SIZE;

	for (Symbol *s = m->buckets[index]; s; s = s->next)
		if (s->name == name)
			return s;
	return NULL;
}

static void map_push(Map *m, Name *name, void *value) {
	Symbol *found, *s, **p;
	int i;

	found = map_find(m, name);
	if (found && found->scope == m->n_scope - 1)
		/* same one in current scope */
		return;

	i = ptrhash(name) % HASHTABLE_SIZE;
	s = make_symbol(m, name, value);

	/* insert into the bucket */
	p = &m->buckets[i];
	s->next = *p;
	*p = s;

	/* swap scope head */
	p = &m->scopes[m->n_scope-1];
	s->cross = *p;
	*p = s;
}

static void free_bucket(Symbol *s)
{
	while (s) {
		Symbol *t = s->next;
		free_symbol(s);
		s = t;
	}
}

static void map_push_scope(Map *m)
{
	m->n_scope++;
	sb_push(m->scopes, NULL);
}

static void map_pop_scope(Map *m)
{
	assert(m->n_scope > 0 && "attempt to pop from empty map!");

	/* free the cross link */
	Symbol *s = m->scopes[m->n_scope-1];
	while (s) {
		int i = ptrhash(s->name) % HASHTABLE_SIZE;
		m->buckets[i] = s->next;

		Symbol *t = s->cross;
		free_symbol(s);
		s = t;
	}

	m->n_scope--;
	sb_len(m->scopes) = m->n_scope;
}

static void push_scope(void)
{
	map_push_scope(&declmap);
	map_push_scope(&typemap);
}

static void pop_scope(void)
{
	map_pop_scope(&declmap);
	map_pop_scope(&typemap);
}

static void map_print(const Map *m)
{
	for (int i = 0; i < m->n_scope; i++) {
		printf("[Scope %d]:", i);

		for (Symbol *s = m->scopes[i]; s; s = s->cross)
			printf(" {%s}", s->name->text);

		printf("\n");
	}
}

static int is_redefinition(Map *m, Name *name)
{
	Symbol *s = map_find(m, name);

	return s && (s->scope == m->n_scope - 1);
}

void traverse(Node *node) {
	Decl *decl;

	switch (node->kind) {
	case ND_FILE:
		for (int i = 0; i < sb_count(node->nodes); i++)
			traverse(node->nodes[i]);
		break;
	case ND_FUNCDECL:
		push_scope();
		for (int i = 0; i < sb_count(node->paramdecls); i++)
			traverse(node->paramdecls[i]);
		traverse(node->body);
		pop_scope();
		break;
	case ND_REFEXPR:
		if (!map_find(&declmap, node->name))
			error("'%s' is not declared", node->name->text);
		break;
	case ND_LITEXPR:
		break;
	case ND_DEREFEXPR:
		traverse(node->expr);
		break;
	case ND_BINEXPR:
		traverse(node->lhs);
		traverse(node->rhs);
		break;
	case ND_EXPRSTMT:
		traverse(node->expr);
		break;
	case ND_PARAMDECL:
		if (is_redefinition(&declmap, node->name))
			error("redefinition of '%s'", node->name->text);

		decl = make_decl(node->name, NULL);
		map_push(&declmap, node->name, decl);
		break;
	case ND_VARDECL:
		if (is_redefinition(&declmap, node->name))
			error("redefinition of '%s'", node->name->text);

		decl = make_decl(node->name, NULL);
		map_push(&declmap, node->name, decl);
		break;
	case ND_DECLSTMT:
		traverse(node->decl);
		break;
	case ND_ASSIGNSTMT:
		/* RHS first */
		traverse(node->rhs);
		traverse(node->lhs);
		break;
	case ND_RETURNSTMT:
		break;
	case ND_COMPOUNDSTMT:
		push_scope();
		for (int i = 0; i < sb_count(node->nodes); i++)
			traverse(node->nodes[i]);
		pop_scope();
		break;
	default:
		fprintf(stderr, "%s: don't know how to traverse node kind %d\n",
		        __func__, node->kind);
		break;
	}
}

static void setup_type_from_str(NameTable *nt, char *s)
{
	Name *n = push_name(nt, s, strlen(s));
	Type *t = make_type(n, NULL);
	map_push(&typemap, n, t);
}

static void setup_builtin_types(NameTable *nt)
{
	setup_type_from_str(nt, "i32");
	setup_type_from_str(nt, "i64");
}

void sema(ASTContext ast)
{
	map_init(&declmap);
	map_init(&typemap);

	// Start by pushing built-in types into the type map.
	setup_builtin_types(ast.nametable);

	traverse(ast.root);

	map_destroy(&declmap);
	map_destroy(&typemap);
}
