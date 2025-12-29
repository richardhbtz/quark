#pragma once
#include <string>
#include <optional>

enum TokenKind
{
    tok_eof,
    tok_number,
    tok_identifier,
    tok_colon,
    tok_equal,
    tok_brace_open,
    tok_brace_close,
    tok_include,
    tok_comma,
    tok_string,
    tok_semicolon,
    tok_paren_open,
    tok_paren_close,
    tok_plus,
    tok_minus,
    tok_mul,
    tok_div,
    tok_mod,     // %
    tok_in,       
    tok_var,    
    tok_if,
    tok_elif,
    tok_else,
    tok_while,
    tok_ret,     // ret keyword
    tok_true,
    tok_false,
    tok_bool,
    tok_int,     // int type (default integer type)
    tok_str,     // str type
    tok_float,   // float type (32-bit floating point)
    tok_double,  // double type (64-bit floating point)
    tok_unknown,
    tok_and,
    tok_or,
    tok_not,
    tok_eq,    // ==
    tok_ne,    // !=
    tok_lt,    // <
    tok_gt,    // >
    tok_le,    // <=
    tok_ge,    // >=
    tok_extern, // extern
    tok_struct, // struct
    tok_dot,    // .
    tok_impl,   // impl
    tok_arrow,  // ->
    tok_data,   // data
    tok_extend, // extend
    tok_this,   // this
    tok_match,  // match
    tok_ampersand, // &
    tok_range,  // ..
    tok_square_bracket_open,  // [
    tok_square_bracket_close, // ]
    tok_break,   // break
    tok_continue // continue
};

struct SourceLocation
{
    int line = 1;
    int column = 1;
    std::string filename = "<input>";
};

struct Token
{
    TokenKind kind;
    double numberValue;
    std::string text;
    SourceLocation location;
};

class Lexer
{
public:
    explicit Lexer(const std::string &src, bool verbose = false, const std::string &filename = "<input>");
    Token next();
    Token peek();
    SourceLocation getCurrentLocation() const { return currentLocation_; }

private:
    std::string src_;
    size_t idx_ = 0;
    Token cur_;
    bool verbose_ = false;
    SourceLocation currentLocation_;
    void skipWhitespace();
    void advancePosition(char c);
};
