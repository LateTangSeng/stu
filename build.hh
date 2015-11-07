#ifndef BUILD_HH
#define BUILD_HH

/* Code for generating rules from a vector of tokens, i.e, for
 * performing the parsing of Stu syntax itself beyond tokenization.
 * This is a recursive descent parser. 
 */

/*
 * Operator precedence.  Higher in the list means higher precedence. 
 *
 * string juxtaposition [not implemented yet]
 * ---------------
 * @...	  (prefix) Phony dependency; argument can only contain name
 * ---------------
 * <...	  (prefix) Input redirection; argument cannot contain '()',
 *        '[]', '$[]' or '@' 
 * ---------------
 * !...	  (prefix) Existence-only; argument cannot contain '$[]'
 * ?...	  ??? (prefix) Optional dependency; argument cannot contain '$[]'
 * ---------------
 * ... * ... ??? (binary) Multiplication; cannot contain '$[]'
 * ---------------
 * [...]   (circumfix) Dynamic dependency; cannot contain '$[]' or '@'
 * (...)   ??? (circumfix) Capture; cannot contain '$[]'
 * $[...]  (circumfix) Variable inclusion; argument cannot contain
 *         '?', '[]', '()', '*' or '@' 
 */

/* 
 * Syntax (in YACC-like notation):
 * 
 * file:                | rule file
 * rule:                target [':' toplevel_dependencies] (COMMAND | ';')
 * target:              ['>'] bare_dependency
 * dependency_list:     | dependency dependency_list
 * dependency:          expression | variable_dependency
 * variable_dependency: '$[' ['!' | '?'] ['<'] NAME ']'
 * expression:          single_expression 
 *                        [ ['*' | CONCAT ] single_expression ]*
 * single_expression:   '[' expression* ']' | '(' expression* ')' 
 *                        | redirect_dependency 
 *                        | '!' single_expression | '?' single_expression 
 * redirect_dependency: ['<'] bare_dependency
 * bare_dependency:     ['@'] NAME
 */

#include <memory>

#include "frmt.hh"

/* An object of this type represents a location within a token list */ 
class Build
{
private:
	vector <shared_ptr <Token> > &tokens;
	vector <shared_ptr <Token> > ::iterator &iter;
	const Place place_end; 

public:

	Build(vector <shared_ptr <Token> > &tokens_,
		  vector <shared_ptr <Token> > ::iterator &iter_,
		  Place place_end_)
		:  tokens(tokens_),
		   iter(iter_),
		   place_end(place_end_)
	{ }
	
	/*
	 * Methods for building the syntax tree:  Each has a name
	 * corresponding to the symbol given
	 * above.  The argument RET is where the result is written.  If the
	 * return value is BOOL, it denotes whether something was read.  On
	 * syntax errors, Logical_Error's are thrown. 
	 */

	/* In some of the following functions, write the input filename into
	 * PLACE_NAME_INPUT.  If 
	 * PLACE_NAME_INPUT is alrady non-empty, throw an error if a second
	 * input filename is specified. 
	 * PLACE_INPUT is the place of the '<' input redirection operator. 
	 */ 

	/* The returned rules may not be unique -- this is checked later */
	void build_file(vector <shared_ptr <Rule> > &ret);

	/* Return NULL when nothing was parsed */ 
	shared_ptr <Rule> build_rule(); 

	void build_dependency_list(vector <shared_ptr <Dependency> > &ret, 
							   Place_Param_Name &place_param_name_input,
							   Place &place_input);

	bool build_dependency(vector <shared_ptr <Dependency> > &ret, 
						  Place_Param_Name &place_param_name_input,
						  Place &place_input);

	shared_ptr <Dependency> build_variable_dependency
	(Place_Param_Name &place_param_name_input,
	 Place &place_input);

	bool build_expression(vector <shared_ptr <Dependency> > &ret, 
						  Place_Param_Name &place_param_name_input,
						  Place &place_input);

	bool build_single_expression(vector <shared_ptr <Dependency> > &ret, 
								 Place_Param_Name &place_param_name_input,
								 Place &place_input);

	shared_ptr <Dependency> build_redirect_dependency
	(Place_Param_Name &place_param_name_input,
	 Place &place_input); 

	/* Whether the next token is the given operator */ 
	bool is_operator(char op) const {
		return 
			iter != tokens.end() &&
			dynamic_pointer_cast <Operator> (*iter) &&
			dynamic_pointer_cast <Operator> (*iter)->op == op;
	}
};

void Build::build_file(vector <shared_ptr <Rule> > &ret)
{
	assert(ret.size() == 0); 
	
	while (iter != tokens.end()) {

#ifndef NDEBUG
		auto iter_begin= iter; 
#endif /* ! NDEBUG */ 

		shared_ptr <Rule> rule= build_rule(); 

		if (rule == NULL) {
			assert(iter == iter_begin); 
			(*iter)->get_place() << "expected a rule"; 
			throw ERROR_LOGICAL;
		}

		ret.push_back(rule); 
	}
}

shared_ptr <Rule> Build::build_rule()
{
	if (! (dynamic_pointer_cast <Param_Name> (*iter) || 
		   is_operator('@') ||
		   is_operator('>'))) {
		return shared_ptr <Rule> (); 
	}

	bool redirect_output= false;
	Place place_output; 

	if (is_operator('>')) {
		place_output= (*iter)->get_place();
		++iter;
		if (iter == tokens.end()) {
			place_end << "expected a filename";
			place_output << "after '>'"; 
			throw ERROR_LOGICAL;
		}
		else if (! (dynamic_pointer_cast <Param_Name> (*iter) || 
					is_operator('@'))) {
			(*iter)->get_place() << "expected a filename";
			place_output << "after '>'"; 
			throw ERROR_LOGICAL;
		}
		redirect_output= true; 
	}

	Place place_target= (*iter)->get_place();

	Type type= T_FILE;
 	if (is_operator('@')) {
		Place place_at= (*iter)->get_place();

		if (redirect_output) {
			place_at << "phony target is invalid";
			place_output << "after '>'"; 
			throw ERROR_LOGICAL;
		}

		++iter;
		if (iter == tokens.end()) {
			place_end << "expected the name of phony target";
			place_at << "after '@'";
			throw ERROR_LOGICAL;
		}
		if (! dynamic_pointer_cast <Param_Name> (*iter)) {
			(*iter)->get_place() << "expected the name of phony target";
			place_at << "after '@'";
			throw ERROR_LOGICAL;
		}

		type= T_PHONY;
	}

	/* Target */ 
	shared_ptr <Name_Token> target_name= dynamic_pointer_cast <Name_Token> (*iter);
	++iter;

	if (! target_name->valid()) {
		place_target <<
			"two parameters must be separated by at least one character";
		throw ERROR_LOGICAL;
	}

	string parameter_duplicate;
	if (target_name->has_duplicate_parameters(parameter_duplicate)) {
		place_target <<
			fmt("target contains duplicate parameter $%s", 
				parameter_duplicate); 
		throw ERROR_LOGICAL;
	}

	shared_ptr <Place_Param_Target> place_param_target
		(new Place_Param_Target(type, *target_name, place_target));

	if (iter == tokens.end()) {
		place_end << "expected a dependency or a command";
		place_target << fmt("after target %s", place_param_target->text()); 
		throw ERROR_LOGICAL;
	}

	vector <shared_ptr <Dependency> > dependencies;

	bool had_colon= false;

	/* Empty at first */ 
	Place_Param_Name filename_input;
	Place place_input;

	if (is_operator(':')) {
		had_colon= true; 
		++iter; 
		build_dependency_list(dependencies, filename_input, place_input); 
	} 

	/* Command */ 
	if (iter == tokens.end()) {
		place_end << "excpected command or ';'";
		place_target << fmt("for target %s", place_param_target->text());
		throw ERROR_LOGICAL;
	}

	/* Remains NULL when there is no command */ 
	shared_ptr <Command> command;
	/* Place of ';' */ 
	Place place_nocommand; 

	if (dynamic_pointer_cast <Command> (*iter)) {
		command= dynamic_pointer_cast <Command> (*iter);
		++iter; 
	} else {
		/* If there is no command there must be ';' */ 
		if (! is_operator(';')) {
			(*iter)->get_place() <<
				(had_colon
				 ? "expected a dependency, a command or ';'"
				 : "expected ':', a command or ';'");
			place_target <<
				fmt("for target %s", place_param_target->text());
			throw ERROR_LOGICAL;
		}
		place_nocommand= (*iter)->get_place(); 
		++iter;
	}

	/* When output redirection is present, the rule must have a command,
	 * and refer to a file (not a phony). */ 
	if (redirect_output) {
		/* Already checked before */ 
		assert(place_param_target->type == T_FILE); 

		if (command == NULL) {
			place_output << 
				"output redirection using '>' must not be used";
			place_nocommand <<
				"in rule without a command";
			throw ERROR_LOGICAL;
		}
	}

	/* When input redirection is present, the rule must have a command */ 
	if (! filename_input.empty()) {
		if (command == NULL) {
			place_input <<
				"input redirection using '<' must not be used";
			place_nocommand <<
				"in rule without a command";
			throw ERROR_LOGICAL;
		}
	}

	shared_ptr <Rule> ret(new Rule(place_param_target, dependencies, 
								   command, redirect_output, filename_input));

	return ret; 
}

void Build::build_dependency_list(vector <shared_ptr <Dependency> > &ret,
								  Place_Param_Name &place_param_name_input,
								  Place &place_input)
{
	/* As a general rule, the places of the generated dependencies are
	 * on the filenames of the dependencies or the '@' of phony
	 * dependencies, and not on prefixes such as '!', '<', etc. 
	 */

	while (iter != tokens.end()) {
		vector <shared_ptr <Dependency> > dependencies_new; 
		bool r= build_dependency(dependencies_new, place_param_name_input, place_input);
		if (!r) {
			assert(dependencies_new.size() == 0); 
			return; 
		}
		ret.insert(ret.end(), dependencies_new.begin(), dependencies_new.end()); 
	}
}

bool Build::build_dependency(vector <shared_ptr <Dependency> > &ret, 
							 Place_Param_Name &place_param_name_input,
							 Place &place_input)
{
	assert(ret.size() == 0); 

	/* Variable dependency */ 
	shared_ptr <Dependency> dependency= 
		build_variable_dependency(place_param_name_input, place_input);
	if (dependency != NULL) {
		ret.push_back(dependency); 
		return true; 
	}

	/* Expression */ 
	bool r= build_expression(ret, place_param_name_input, place_input); 
	if (!r)
		return false;

	return true;
}

shared_ptr <Dependency> Build::build_variable_dependency
(Place_Param_Name &place_param_name_input, 
 Place &place_input)
{
	bool has_input= false;

	shared_ptr <Dependency> ret;

	if (! is_operator('$')) 
		return shared_ptr <Dependency> ();

	Place place_dollar= (*iter)->get_place();
	++iter;

	if (iter == tokens.end()) {
		place_end << "expected '['";
		place_dollar << "after '$'"; 
		throw ERROR_LOGICAL;
	}
	
	if (! is_operator('[')) 
		return shared_ptr <Dependency> ();

	++iter;

	Flags flags= F_VARIABLE;

	/* Optional '!' */ 
	Place place_exclam;
	if (is_operator('!')) {
		place_exclam= (*iter)->get_place();
		flags |= F_EXISTENCE; 
		++iter;
	}

	/* Input redirection with '<' */ 
	if (is_operator('<')) {
		has_input= true;
		place_input= (*iter)->get_place(); 
		++iter;
	}
	
	/* Name of variable dependency */ 
	if (! dynamic_pointer_cast <Param_Name> (*iter)) {
		(*iter)->get_place() << "expected a filename";
		if (has_input)
			place_input << "after '<'";
		else if (flags & F_EXISTENCE) 
			place_exclam << "after '!'";
		else
			place_dollar << "after '$['";

		throw ERROR_LOGICAL;
	}
	shared_ptr <Place_Param_Name> place_param_name= 
		dynamic_pointer_cast <Place_Param_Name> (*iter);
	++iter;

	if (has_input && ! place_param_name_input.empty()) {
		place_param_name->place << 
			fmt("duplicate input redirection <%s", place_param_name->unparametrized());
		place_param_name_input.place << 
			fmt("shadows previous input redirection <%s", place_param_name_input.unparametrized()); 
		throw ERROR_LOGICAL;
	}

	/* Check that the name does not contain '=' */ 
	for (auto j= place_param_name->get_texts().begin();
		 j != place_param_name->get_texts().end();  ++j) {
		if (j->find('=') != string::npos) {
			place_param_name->place <<
				"name of variable dependency must not contain '='"; 
			throw ERROR_LOGICAL;
		}
	}

	/* Closing ']' */ 
	if (iter == tokens.end()) {
		place_end << "expected ']'";
		place_dollar << "after opening '$['";
		throw ERROR_LOGICAL;
	}
	if (! is_operator(']')) {
		(*iter)->get_place() << "expected ']'";
		place_dollar << "after opening '$['";
		throw ERROR_LOGICAL;
	}
	++iter;

	if (has_input) {
		place_param_name_input= *place_param_name;
	}
	
	return shared_ptr <Dependency> 
		(new Direct_Dependency
		 (flags,
		  Place_Param_Target(T_FILE, *place_param_name, place_dollar)));
}

bool Build::build_expression(vector <shared_ptr <Dependency> > &ret, 
							 Place_Param_Name &place_param_name_input,
							 Place &place_input)
{
	assert(ret.size() == 0); 

	bool r= build_single_expression(ret, place_param_name_input, place_input);

	if (! r)
		return false; 

	return true;
}

bool Build::build_single_expression(vector <shared_ptr <Dependency> > &ret, 
									Place_Param_Name &place_param_name_input,
									Place &place_input)
{
	assert(ret.size() == 0); 

	/* '(' expression* ')' */ 
	if (iter != tokens.end() && is_operator('(')) {
		Place place_paren= (*iter)->get_place();
		++iter;
		vector <shared_ptr <Dependency> > r;
		while (build_expression(r, place_param_name_input, place_input)) {
			ret.insert(ret.end(), r.begin(), r.end()); 
			r.clear(); 
		}
		if (iter == tokens.end()) {
			place_end << "expected ')'";
			place_paren << "for group started by '('"; 
			throw ERROR_LOGICAL;
		}
		if (! is_operator(')')) {
			(*iter)->get_place() << "expected ')'";
			place_paren << "for group started by '('"; 
			throw ERROR_LOGICAL;
		}
		++ iter; 
		return true; 
	} 

	/* '[' expression* ']' */
	if (iter != tokens.end() && is_operator('[')) {
		Place place_paren= (*iter)->get_place();
		++iter;	
		vector <shared_ptr <Dependency> > r2;
		vector <shared_ptr <Dependency> > r;
		while (build_expression(r, place_param_name_input, place_input)) {
			r2.insert(r2.end(), r.begin(), r.end()); 
			r.clear(); 
		}
		if (iter == tokens.end()) {
			place_end << "expected ']'";
			place_paren << "for group started by '['"; 
			throw ERROR_LOGICAL;
		}
		if (! is_operator(']')) {
			(*iter)->get_place() << "expected ']'";
			place_paren << "for group started by '['"; 
			throw ERROR_LOGICAL;
		}
		++ iter; 
		for (auto j= r2.begin();  j != r2.end();  ++j) {
			shared_ptr <Dependency> dependency_new= 
				make_shared <Dynamic_Dependency> (0, *j);
			ret.push_back(dependency_new);
		}
		return true; 
	} 

	/* '!' single_expression */ 
 	if (iter != tokens.end() && is_operator('!')) {
 		Place place_exclam= (*iter)->get_place();
 		++iter; 
		if (! build_single_expression(ret, place_param_name_input, place_input)) {
			if (iter == tokens.end()) {
				place_end << "expected a dependency";
				place_exclam << "after '!'";
				throw ERROR_LOGICAL;
			} else {
				(*iter)->get_place() << "expected a dependency";
				place_exclam << "after '!'"; 
				throw ERROR_LOGICAL;
			}
		}
		for (auto j= ret.begin();  j != ret.end();  ++j) {
			(*j)->add_flags(F_EXISTENCE);
			(*j)->set_place_existence(place_exclam); 
		}
		return true;
	}

	/* '?' single_expression */ 
 	if (iter != tokens.end() && is_operator('?')) {
 		Place place_question= (*iter)->get_place();
 		++iter; 
		if (! build_single_expression(ret, place_param_name_input, place_input)) {
			if (iter == tokens.end()) {
				place_end << "expected a dependency";
				place_question << "after '?'"; 
				throw ERROR_LOGICAL;
			} else {
				(*iter)->get_place() << "expected a dependency";
				place_question << "after '?'"; 
				throw ERROR_LOGICAL;
			}
		}
		for (auto j= ret.begin();  j != ret.end();  ++j) {

			/* D_INPUT and D_OPTIONAL cannot be used at the same
			 * time.  Note:  Input redirection is not allowed in dynamic
			 * dependencies, and therefore it is sufficient to check
			 * this here.  */  
			if (place_param_name_input.place.type != Place::P_EMPTY) {
				place_input <<
					"input redirection using '<' must not be used";
				place_question <<
					"in conjunction with optional dependencies using '?'"; 
				throw ERROR_LOGICAL;
			}

			/* That '?' and '!' cannot be used together is checked
			 * within Execution and not here, because these can also
			 * come from dynamic dependencies. */

			(*j)->add_flags(F_OPTIONAL); 
			(*j)->set_place_optional(place_question); 
		}
		return true;
	}
		
	shared_ptr <Dependency> r= 
		build_redirect_dependency(place_param_name_input, place_input); 
	if (r != NULL) {
		ret.push_back(r);
		return true; 
	}

	return false;
}

shared_ptr <Dependency> Build::build_redirect_dependency
(Place_Param_Name &place_param_name_input,
 Place &place_input)
{
	bool has_input= false;

	if (is_operator('<')) {
		place_input= (*iter)->get_place();

		++iter;
		has_input= true; 
	}

	bool has_phony= false;
	Place place_at; 
	if (is_operator('@')) {
		place_at= (*iter)->get_place();
		if (has_input) {
			place_at << "expected a filename";
			place_input << "after '<'";
			throw ERROR_LOGICAL;
		}
		++ iter;
		has_phony= true; 
	}

	if (iter == tokens.end()) {
		if (has_input) {
			place_end << "expected a filename";
			place_input << "after '<'"; 
			throw ERROR_LOGICAL;
		} else if (has_phony) {
			place_end << "expected the name of a phony target";
			place_at << "after '@'"; 
			throw ERROR_LOGICAL; 
		} else {
			return shared_ptr <Dependency> (); 
		}
	}

	if (NULL == dynamic_pointer_cast <Name_Token> (*iter)) {
		if (has_input) {
			(*iter)->get_place() << "expected a filename";
			place_input << "after '<'"; 
			throw ERROR_LOGICAL;
		} else if (has_phony) {
			(*iter)->get_place() << "expected the name of a phony target";
			place_at << "after '@'"; 
			throw ERROR_LOGICAL;
		} else {
			return shared_ptr <Dependency> (); 
		}
	}

	shared_ptr <Name_Token> name_token= dynamic_pointer_cast <Name_Token> (*iter);
	++iter; 

	if (has_input && ! place_param_name_input.empty()) {
		name_token->place << 
			fmt("duplicate input redirection <%s", 
				name_token->unparametrized());
		place_param_name_input.place << 
			fmt("shadows previous input redirection <%s", 
				place_param_name_input.unparametrized()); 
		throw ERROR_LOGICAL;
	}

	Flags flags= 0;
	if (has_input) {
		assert(place_param_name_input.empty()); 
		place_param_name_input= *name_token;
	}

	shared_ptr <Dependency> ret
		(new Direct_Dependency
		 (flags,
		  Place_Param_Target(has_phony ? T_PHONY : T_FILE,
							 *name_token,
							 has_phony ? place_at : name_token->place))); 

	return ret; 
}

#endif /* ! BUILD_HH */