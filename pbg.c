#include "pbg.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/********************
 * Helper functions *
 ********************/

/**
 * Checks if the given character is a digit.
 * @param c  Character to check.
 */
int is_a_digit(char c)
{
	return c >= '0' && c <= '9';
}

/**
 * Static nodes are indexed by 0,1,2,... Dynamic nodes are indexed by 
 * -1,-2,-3,..., where these correspond to the first, second, third, etc nodes 
 * in the list. As such, this function converts the index to an array index in 
 * either list depending on its sign and value.
 * @param e      PBG expression to search.
 * @param index  Index of the node to return.
 * @return Pointer to the pbg_expr_node* in e specified by the index.
 */
pbg_expr_node* get_expr_node(pbg_expr* e, int index)
{
	if(index < 0)
		return e->_dynamic + -(index+1);
	else
		return e->_static + index;
}

/**
 * Free's the single pbg_expr_node pointed to by the specified pointer.
 * @param node  pbg_expr_node to free.
 */
void free_expr_node(pbg_expr_node* node)
{
	/* These are literals and operators for which _data is not malloc'd. */
	if(node->_type == PBG_LT_TRUE)
		return;
	if(node->_type == PBG_LT_FALSE)
		return;
	if(node->_type == PBG_UNKNOWN)
		return;
	
	/* For anything else, _data is malloc'd. Free it. */
	free(node->_data);
}

/**
 * These structures are used by pbg_parse_r during recursive calls to track the 
 * current field in the expression string, the current closing brace in the 
 * expression string, and the index of the node created during the call.
 */
typedef struct {
	int  field;    /* Index of current field. */
	int  closing;  /* Index of next closing. */
} int2arg;
typedef struct {
	int2arg  arg;      /* Argument data. */
	int      nodeidx;  /* Index of created node. */
} int2ret;


/**********************
 * Core API Functions *
 **********************/

/**
 * @param fields    Indices of each field in str.
 * @param lengths   Lengths of each field in str.
 * @param closings  Indices of closing parentheses in str.
 * @param arg       Indices of current field and current closing.
 */
int2ret pbg_parse_r(pbg_expr* e, char* str, int* fields, int* lengths, int* closings, int2arg arg)
{
	pbg_expr_node* node;  /* Alias for node being constructed. */
	int2ret        ret;   /* Return values from this function invokation. */
	
	/* Identify type of operator this node represents, if any. */
	pbg_node_type type = pbg_toop(str+fields[arg.field], lengths[arg.field]);
	
	/* This field is an operator. */
	if(type != PBG_UNKNOWN) {
		/* Initialize node and record node index. */
		int nodeidx = e->_staticsz++;
		node = e->_static + nodeidx;
		node->_type = type;
		node->_int = 0;
		
		/* We've processed this field. Move on to the next one! */
		arg.field++;
		
		/* Allocate memory for pointers to children nodes. */
		int maxchildren = 2;
		int* children = (int*) malloc(maxchildren * sizeof(int));
		if(children == NULL) {
			// TODO error handling
		}
		
		/* Recursively build subtree rooted at this operator node. */
		/* pbg_evaluate set last element in fields to -1. This is used to
		 * ensure we don't run past the end of the string. */
		ret.arg = arg;
		while(fields[ret.arg.field] != -1 && 
				fields[ret.arg.field] < closings[ret.arg.closing]) {
			ret = pbg_parse_r(e, str, fields, lengths, closings, ret.arg);
			/* Expand array of children if necessary. */
			if(node->_int == maxchildren) {
				children = (int*) realloc(children, (maxchildren *= 2) * sizeof(int));
				if(children == NULL) {
					// TODO error handling
				}
			}
			/* Store index of child node. */
			children[node->_int++] = ret.nodeidx;
		}
		
		/* Tighten list of children and save it. */
		children = (int*) realloc(children, node->_int * sizeof(int));
		if(children == NULL) {
			// TODO error handling
		}
		node->_data = (void*) children;
		
		/* Build return values. Static nodes have positive indices. */
		ret.nodeidx = nodeidx;
		
		/* This node read all of its children until the next closing. 
		 * The parent node will need to read until the end of the next 
		 * next one. */
		ret.arg.closing++;
		
	/* This field is a literal. */
	}else{
		/* Cache length of field for easier referencing. */
		int n = lengths[arg.field];
		/* Move str to correct starting position. */
		str += fields[arg.field];
		
		/* KEY. Copy key identifier into string. */
		if(pbg_iskey(str, n)) {
			/* Dynamic nodes have negative indices. */
			/* Subtract 1 to offset first element to -1 from 0. */
			ret.nodeidx = -(e->_dynamicsz++)-1;
			node = e->_dynamic - (ret.nodeidx+1);
			/* Initialize node! */
			node->_type = PBG_LT_KEY;
			node->_int = (n-2) * sizeof(char);
			node->_data = malloc(node->_int);
			if(node->_data == NULL) {
				// TODO failed to allocate memory for node
			}
			strncpy((char*)node->_data, str+1, n-2);
			// TODO ensure each key node in dynamic is unique?
			
		/* DATE. Convert to PBG DATE constant. */
		}else if(pbg_isdate(str, n)) {
			/* Static nodes have positive indices. */
			ret.nodeidx = e->_staticsz++;
			node = e->_static + ret.nodeidx;
			/* Initialize node. */
			node->_type = PBG_LT_DATE;
			node->_int = sizeof(pbg_type_date);
			node->_data = malloc(node->_int);
			if(node->_data == NULL) {
				// TODO failed to allocate memory for node
			}
			pbg_todate((pbg_type_date*)node->_data, str, n);
			
		/* NUMBER. Parse entire element as a float. */
		}else if(pbg_isnumber(str, n)) {
			/* Static nodes have positive indices. */
			ret.nodeidx = e->_staticsz++;
			node = e->_static + ret.nodeidx;
			/* Initialize node! */
			node->_type = PBG_LT_NUMBER;
			node->_int = sizeof(double);
			node->_data = malloc(node->_int);
			if(node->_data == NULL) {
				// TODO failed to allocate memory for node
			}
			*((double*)node->_data) = atof(str);
			
		/* STRING. Copy everything between single quotes. */
		}else if(pbg_isstring(str, n)) {
			/* Static nodes have positive indices. */
			ret.nodeidx = e->_staticsz++;
			node = e->_static + ret.nodeidx;
			/* Initialize node! */
			node->_type = PBG_LT_STRING;
			node->_int = (n-2) * sizeof(char);
			node->_data = malloc(node->_int);
			if(node->_data == NULL) {
				// TODO failed to allocate memory for node
			}
			strncpy((char*)node->_data, str+1, n-2);
			
		/* TRUE. No need for any data. */
		}else if(pbg_istrue(str, n)) {
			/* Static nodes have positive indices. */
			ret.nodeidx = e->_staticsz++;
			node = e->_static + ret.nodeidx;
			/* Initialize node! */
			node->_type = PBG_LT_TRUE;
			node->_int = 0;
			node->_data = NULL;
			
		/* FALSE. No need for any data. */
		}else if(pbg_isfalse(str, n)) {
			/* Static nodes have positive indices. */
			ret.nodeidx = e->_staticsz++;
			node = e->_static + ret.nodeidx;
			/* Initialize node! */
			node->_type = PBG_LT_FALSE;
			node->_int = 0;
			node->_data = NULL;
			
		}else{
			// TODO error: not a valid literal
		}
		
		/* This field has been processed. Move to the next one! */
		arg.field++;
		ret.arg = arg;
	}
	
	/* Done! */
	return ret;
}

int pbg_parse(pbg_expr* e, char* str, int n)
{
	// TODO verify str is an element of PBG (syntactically, not semantically)
	
	/* Count number of non-STRING commas, keys, and closing braces. */
	int numcommas = 0;
	int numkeys = 0;
	int numclosings = 0;
	int instring = 0;
	for(int i = 0; i < n; i++) {
		/* Check if we're in a string or not. */
		if((!instring && str[i] == '\'') || 
				(instring && str[i] == '\'' && str[i-1] != '\\'))
			instring = 1 - instring;
		/* Nothing gets counted if we are in a string! */
		if(!instring) {
			if(str[i] == ',') numcommas++;
			else if(str[i] == '[') numkeys++;
			else if(str[i] == ')') numclosings++;
		}
	}
	
	/* Compute sizes of static and dynamic arrays. */
	/* The relevant fields in e are used as indexing each array during parsing. */
	int numfields = numcommas+1;
	int numstatic = numfields - numkeys;
	int numdynamic = numkeys;
	
	/* Allocate space for needed arrays. */
	int* fields = (int*) malloc((numfields+1) * sizeof(int));
	if(fields == NULL) { /* TODO error handling */ }
	int* lengths = (int*) malloc(numfields * sizeof(int));
	if(lengths == NULL) { /* TODO error handling */ }
	int* closings = (int*) malloc(numclosings * sizeof(int));
	if(closings == NULL) { /* TODO error handling */ }
	
	/* Compute position and lengths of each field & position of closings. */
	fields[0] = (str[0] == '(') ? 1 : 0;
	for(int i=fields[0], c=0, f=0, open=1, instring=0; i < n; i++) {
		/* Check if we're in a string or not. */
		if((!instring && str[i] == '\'') || 
				(instring && str[i] == '\'' && str[i-1] != '\\'))
			instring = 1 - instring;
		/* Nothing gets opened, closed, or measured if we're in a string! */
		if(!instring) {
			if(str[i] == ')')
				closings[c] = i;
			if(open && (str[i] == ')' || (str[i] == ',' && str[i-1] != ')')))
				lengths[f] = i - fields[f], f++, open = 0;
			if(!open && (str[i] == '(' || (str[i] == ',' && str[i+1] != '(')))
				fields[f] = i+1, open = 1;
		}
	}
	/* Needed to determine when we've reached the end of the expression string
	 * in pbg_parse_r. */
	fields[numfields] = -1;
	
	/* Allocate space for static and dynamic node arrays. */
	e->_static = (pbg_expr_node*) malloc(numstatic * sizeof(pbg_expr_node));
	e->_dynamic = (pbg_expr_node*) malloc(numdynamic * sizeof(pbg_expr_node));
	
	/* These are initialized to 0 as they are used as counters for the number 
	 * of each type of node created. In the end they should be equal to the 
	 * associated local variables here. */
	e->_staticsz = 0;
	e->_dynamicsz = 0;
	
	/* Recursively parse the expression string to build the expression tree. */
	pbg_parse_r(e, str, fields, lengths, closings, (int2arg) { 0, 0 });
	
	/* Clean up! */
	free(fields);
	free(lengths);
	free(closings);
}


int pbg_evaluate_r(pbg_expr* e, pbg_expr_node* node)
{
	/* This is a literal node. */
	if(node->_type < PBG_MAX_LT) {
		if(node->_type == PBG_LT_TRUE)  return 1;
		if(node->_type == PBG_LT_FALSE) return 0;
		return 0;  // TODO the only literals with truth values are TRUE and FALSE.
		
	/* This is an operator node. */
	}else if(node->_type < PBG_MAX_OP) {
		int* children = (int*) node->_data;
		int size = node->_int;
		pbg_expr_node* child0, *child1, *childi;
		pbg_expr_node keylt;
		child0 = get_expr_node(e, children[0]);
		switch(node->_type) {
			/* NOT: invert the truth value of the contained expression. */
			case PBG_OP_NOT:
				return (pbg_evaluate_r(e, child0) == 0);
				break;
			/* AND: true only if all subexpressions are true. */
			case PBG_OP_AND:
				for(int i = 0; i < size; i++)
					if(pbg_evaluate_r(e, get_expr_node(e, children[i])) == 0)
						return 0;
				return 1;
				break;
			/* OR: true if any of the subexpressions are true. */
			case PBG_OP_OR:
				for(int i = 0; i < size; i++)
					if(pbg_evaluate_r(e, get_expr_node(e, children[i])) == 1)
						return 1;
				return 0;
				break;
			/* EQ: true only all children are equal to each other. */
			case PBG_OP_EQ:
				/* Ensure type and size of all children are identical. */
				for(int i = 1; i < size; i++) {
					childi = get_expr_node(e, children[i]);
					if(childi->_int != child0->_int || 
							childi->_type != child0->_type)
						return 0;
					/* Ensure each data byte is identical. */
					for(int j = 0; j < child0->_int; j++)
						if(((char*)child0->_data)[j] != ((char*)childi->_data)[j])
							return 0;
				}
				return 1;
				break;
			/* LT: true only if the first child is less than the second. */
			case PBG_OP_LT:
				child1 = get_expr_node(e, children[1]);
				return *((double*)child0->_data) < *((double*)child1->_data);
				break;
			/* GT: true only if the first child is greater than the second. */
			case PBG_OP_GT:
				child1 = get_expr_node(e, children[1]);
				return *((double*)child0->_data) > *((double*)child1->_data);
				break;
			/* EXST: true only if the KEY exists in the given dictionary. */
			case PBG_OP_EXST:
				return child0->_type != PBG_UNKNOWN;  /* elegant. */
				break;
			/* NEQ: true only if the two children are different. */
			case PBG_OP_NEQ:
				child1 = get_expr_node(e, children[1]);
				return child1->_type != child0->_type || 
						child1->_int != child0->_int || 
						strncmp(child1->_data, child0->_data, child0->_int);
				break;
			/* LTE: true only if the first child is at most the second. */
			case PBG_OP_LTE:
				child1 = get_expr_node(e, children[1]);
				return *((double*)child0->_data) <= *((double*)child1->_data);
				break;
			/* GTE: true only if the first child is at least the second. */
			case PBG_OP_GTE:
				child1 = get_expr_node(e, children[1]);
				return *((double*)child0->_data) >= *((double*)child1->_data);
				break;
		}
	}else{
		// TODO should never get here!
	}
}

int pbg_evaluate(pbg_expr* e, pbg_expr_node (*dict)(char*, int))
{
	/* KEY resolution. Lookup every key in provided dictionary. */
	pbg_expr_node* dynamics;
	dynamics = (pbg_expr_node*) malloc(e->_dynamicsz * sizeof(pbg_expr_node));
	if(dynamics == NULL) {
		// TODO error handling
	}
	for(int i = 0; i < e->_dynamicsz; i++) {
		pbg_expr_node* keylt = e->_dynamic+i;
		dynamics[i] = dict((char*)(keylt->_data), keylt->_int);
	}

	/* Swap out KEY literals with dictionary equivalents. */
	pbg_expr_node* old = e->_dynamic;
	e->_dynamic = dynamics;

	/* Evaluate expression! */
	int result = pbg_evaluate_r(e, e->_static);

	/* Restore old KEY literals. */
	e->_dynamic = old;

	/* Clean up malloc'd memory. */
	for(int i = 0; i < e->_dynamicsz; i++)
		free_expr_node(dynamics+i);
	free(dynamics);

	/* Done! */
	return result;
}


void pbg_free(pbg_expr* e)
{
	/* Free individual static nodes. Some do not have _data malloc'd. */
	for(int i = e->_staticsz-1; i >= 0; i--)
		free_expr_node(e->_static+i);
	
	/* Free individual dynamic nodes. All have _data malloc'd. */
	for(int i = 0; i < e->_dynamicsz; i++)
		free(e->_dynamic[i]._data);
	
	/* Free internal node arrays. */
	free(e->_static);
	free(e->_dynamic);
}


int pbg_gets_r(pbg_expr* e, pbg_expr_node* node, char* buf, int i)
{
	/* This node is a literal. */
	if(node->_type < PBG_MAX_LT)
	{
		char* data;
		pbg_type_date* dt;
		switch(node->_type) {
			case PBG_LT_TRUE:
				buf[i++] = 'T';
				buf[i++] = 'R';
				buf[i++] = 'U';
				buf[i++] = 'E';
				break;
			case PBG_LT_FALSE:
				buf[i++] = 'F';
				buf[i++] = 'A';
				buf[i++] = 'L';
				buf[i++] = 'S';
				buf[i++] = 'E';
				break;
			case PBG_LT_STRING:
				data = (char*) node->_data;
				buf[i++] = '\'';
				for(int j = 0; j < node->_int; j++)
					buf[i++] = data[j];
				buf[i++] = '\'';
				break;
			case PBG_LT_DATE:
				dt = (pbg_type_date*) node->_data;
				i += snprintf(buf+i, 5, "%04d", dt->_YYYY);
				buf[i++] = '-';
				i += snprintf(buf+i, 3, "%02d", dt->_MM);
				buf[i++] = '-';
				i += snprintf(buf+i, 3, "%02d", dt->_DD);
				break;
			case PBG_LT_KEY:
				data = (char*) node->_data;
				buf[i++] = '[';
				for(int j = 0; j < node->_int; j++)
					buf[i++] = data[j];
				buf[i++] = ']';
				break;
			case PBG_LT_NUMBER:
				i += sprintf(buf+i, "%.2lf", *((double*)node->_data));
				break;
			default:
				// TODO unknown operator!
				break;
		}
	
	/* This node is an operator. */
	}else if(node->_type < PBG_MAX_OP) {
		buf[i++] = '(';
		switch(node->_type) {
			case PBG_OP_NOT:  buf[i++] = '!'; break;
			case PBG_OP_AND:  buf[i++] = '&'; break;
			case PBG_OP_OR:   buf[i++] = '|'; break;
			case PBG_OP_EQ:   buf[i++] = '='; break;
			case PBG_OP_LT:   buf[i++] = '<'; break;
			case PBG_OP_GT:   buf[i++] = '>'; break;
			case PBG_OP_EXST: buf[i++] = '?'; break;
			case PBG_OP_NEQ:  buf[i++] = '!', buf[i++] = '='; break;
			case PBG_OP_LTE:  buf[i++] = '<', buf[i++] = '='; break;
			case PBG_OP_GTE:  buf[i++] = '>', buf[i++] = '='; break;
			default:
				// TODO unknown operator!
				break;
		}
		buf[i++] = ',';
		int* children = (int*)node->_data;
		for(int j = 0; j < node->_int; j++) {
			pbg_expr_node* child = get_expr_node(e, children[j]);
			i = pbg_gets_r(e, child, buf, i);
			if(j != node->_int-1)
				buf[i++] = ',';
		}
		buf[i++] = ')';
	}
	return i;
}

char* pbg_gets(pbg_expr* e, char** bufptr, int n)
{
	char* buf = (char*) malloc(1000);  // TODO fix me then mix me
	int len = pbg_gets_r(e, e->_static, buf, 0);
	buf[len] = '\0';
	return buf;
}

void pbg_print_h(pbg_expr* e, pbg_expr_node* node, int depth)
{
	for(int i = 0; i < depth; i++)
		printf("  ");
	if(node->_type < PBG_MAX_LT) {
		pbg_type_date* date;
		switch(node->_type) {
			case PBG_LT_TRUE:
				printf("TRUE\n");
				break;
			case PBG_LT_FALSE:
				printf("FALSE\n");
				break;
			case PBG_LT_NUMBER:
				printf("NUMBER : %lf\n", *((double*)node->_data));
				break;
			case PBG_LT_STRING:
				printf("STRING : '");
				for(int i = 0; i < node->_int; i++)
					printf("%c", ((char*)node->_data)[i]);
				printf("'\n");
				break;
			case PBG_LT_DATE:
				date = (pbg_type_date*)node->_data;
				printf("DATE : %4d-%2d-%2d\n", date->_YYYY, date->_MM, date->_DD);
				break;
			case PBG_LT_KEY:
				printf("KEY : [");
				for(int i = 0; i < node->_int; i++)
					printf("%c", ((char*)node->_data)[i]);
				printf("]\n");
				break;
			default:
				printf("UNKNWON LT\n");
				break;
		}
	}else if(node->_type < PBG_MAX_OP) {
		switch(node->_type) {
			case PBG_OP_NOT: printf("NOT !\n"); break;
			case PBG_OP_AND: printf("AND &\n"); break;
			case PBG_OP_OR: printf("OR |\n"); break;
			case PBG_OP_EQ: printf("EQ =\n"); break;
			case PBG_OP_LT: printf("LT <\n"); break;
			case PBG_OP_GT: printf("GT >\n"); break;
			case PBG_OP_EXST: printf("EXIST ?\n"); break;
			case PBG_OP_NEQ: printf("NEQ !=\n"); break;
			case PBG_OP_LTE: printf("LTE <=\n"); break;
			case PBG_OP_GTE: printf("GTE >=\n"); break;
			default: printf("UNKNOWN OP\n"); break;
		}
		int i = 0, size = node->_int;
		int* children = (int*) node->_data;
		while(i != size) {
			if(children[i] < 0)
				pbg_print_h(e, e->_dynamic + (-children[i++]-1), depth+1);
			else
				pbg_print_h(e, e->_static + children[i++], depth+1);
		}
	}
}

void pbg_print(pbg_expr* e)
{
	pbg_print_h(e, e->_static, 0);
}


/*************************************
 * Conversion and checking functions *
 *************************************/

pbg_node_type pbg_toop(char* str, int n)
{
	if(n == 1) {
		if(str[0] == '!')      return PBG_OP_NOT;
		else if(str[0] == '&') return PBG_OP_AND;
		else if(str[0] == '|') return PBG_OP_OR;
		else if(str[0] == '=') return PBG_OP_EQ;
		else if(str[0] == '<') return PBG_OP_LT;
		else if(str[0] == '>') return PBG_OP_GT;
		else if(str[0] == '?') return PBG_OP_EXST;
	}else if(n == 2) {
		if(str[0] == '!' && str[1] == '=')      return PBG_OP_NEQ;
		else if(str[0] == '<' && str[1] == '=') return PBG_OP_LTE;
		else if(str[0] == '>' && str[1] == '=') return PBG_OP_GTE;
	}
	return PBG_UNKNOWN;
}


int pbg_istrue(char* str, int n)
{
	return n == 4 && 
		str[0] == 'T' && 
		str[1] == 'R' && 
		str[2] == 'U' && 
		str[3] == 'E';
}


int pbg_isfalse(char* str, int n)
{
	return n == 5 && 
		str[0] == 'F' && 
		str[1] == 'A' && 
		str[2] == 'L' && 
		str[3] == 'S' && 
		str[4] == 'E';
}


int pbg_isnumber(char* str, int n)
{
	int i = 0;
	
	/* Check if negative or positive */
	if(str[i] == '-' || str[i] == '+') i++;
	/* Otherwise, ensure first character is a digit. */
	else if(!is_a_digit(str[i]))
		return 0;
	
	/* Parse everything before the dot. */
	if(str[i] != '0' && is_a_digit(str[i])) {
		while(i != n && is_a_digit(str[i])) i++;
		if(i != n && !is_a_digit(str[i]) && str[i] != '.') return 0;
	}else if(str[i] == '0') {
		if(++i != n && !(str[i] == '.' || str[i] == 'e' || str[i] == 'E')) return 0;
	}
	
	/* Parse everything after the dot. */
	if(str[i] == '.') {
		/* Last character must be a digit. */
		if(i++ == n-1) return 0;
		/* Exhaust all digits. */
		while(i != n && is_a_digit(str[i])) i++;
		if(i != n && !is_a_digit(str[i]) && str[i] != 'e' && str[i] != 'E') return 0;
	}
	
	/* Parse everything after the exponent. */
	if(str[i] == 'e' || str[i] == 'E') {
		/* Last character must be a digit. */
		if(i++ == n-1) return 0;
		/* Parse positive or negative sign. */
		if(str[i] == '-' || str[i] == '+') i++;
		/* Exhaust all digits. */
		while(i != n && is_a_digit(str[i])) i++;
		if(i != n && !is_a_digit(str[i])) return 0;
	}
	
	/* Probably a number! */
	return 1;
}


int pbg_iskey(char* str, int n)
{
	return str[0] == '[' && str[n-1] == ']';
}


int pbg_isstring(char* str, int n)
{
	return str[0] == '\'' && str[n-1] == '\'';
}


int pbg_isdate(char* str, int n)
{
	return n == 10 &&
		str[0] >= '0' && str[0] <= '9' && 
		str[1] >= '0' && str[1] <= '9' && 
		str[2] >= '0' && str[2] <= '9' && 
		str[3] >= '0' && str[3] <= '9' && 
		str[4] == '-' && 
		str[5] >= '0' && str[5] <= '9' && 
		str[6] >= '0' && str[6] <= '9' && 
		str[7] == '-' && 
		str[8] >= '0' && str[8] <= '9' && 
		str[9] >= '0' && str[9] <= '9';
}


void pbg_todate(pbg_type_date* ptr, char* str, int n)
{
	/* Year. */
	ptr->_YYYY = (str[0]-'0')*1000 + (str[1]-'0')*100 + (str[2]-'0')*10 + (str[3]-'0');
	/* Month. */
	ptr->_MM = (str[5]-'0')*10 + (str[6]-'0');
	/* Day. */
	ptr->_DD = (str[8]-'0')*10 + (str[9]-'0');
	// TODO enforce ranges on months and days
}
