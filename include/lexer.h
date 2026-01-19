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
    tok_include,  // 'import' keyword
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
    tok_int,     // int type (default integer type, same as i32)
    tok_i8,      // i8 type (8-bit signed integer)
    tok_i16,     // i16 type (16-bit signed integer)
    tok_i32,     // i32 type (32-bit signed integer)
    tok_i64,     // i64 type (64-bit signed integer)
    tok_u8,      // u8 type (8-bit unsigned integer)
    tok_u16,     // u16 type (16-bit unsigned integer)
    tok_u32,     // u32 type (32-bit unsigned integer)
    tok_u64,     // u64 type (64-bit unsigned integer)
    tok_str,     // str type
    tok_float,   // float type (32-bit floating point)
    tok_double,  // double type (64-bit floating point)
    tok_char,    // char type (8-bit character)
    tok_char_literal, // character literal 'x'
    tok_float_literal, // float literal with f suffix (e.g., 1.0f)
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
    tok_continue, // continue
    tok_plus_eq,  // +=
    tok_minus_eq, // -=
    tok_mul_eq,   // *=
    tok_div_eq,   // /=
    tok_mod_eq,   // %=
    tok_null,     // null
    tok_fat_arrow, // =>
    tok_underscore, // _ (wildcard)
    tok_module,     // module declaration
    tok_fn,         // fn keyword for function pointer types
    tok_void,       // void keyword
    // Bitwise operators
    tok_bitwise_or,   // |
    tok_bitwise_xor,  // ^
    tok_bitwise_not,  // ~
    tok_shift_left,   // <<
    tok_shift_right,  // >>
    tok_bitwise_or_eq,  // |=
    tok_bitwise_xor_eq, // ^=
    tok_bitwise_and_eq, // &=
    tok_shift_left_eq,  // <<=
    tok_shift_right_eq, // >>=
    tok_map,            // map keyword
    tok_list            // list keyword
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
