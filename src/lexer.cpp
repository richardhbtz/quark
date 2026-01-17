#include "../include/lexer.h"
#include <cctype>

Lexer::Lexer(const std::string &src, bool verbose, const std::string &filename) : src_(src), verbose_(verbose)
{
    currentLocation_.filename = filename;

    if (src_.size() >= 3 &&
        static_cast<unsigned char>(src_[0]) == 0xEF &&
        static_cast<unsigned char>(src_[1]) == 0xBB &&
        static_cast<unsigned char>(src_[2]) == 0xBF)
    {
        idx_ = 3;
    }

    cur_ = next();
    if (verbose_)
    {

        auto t = cur_;
        printf("[lexer]first token kind=%d text='%s' number=%g at %d:%d\n", (int)t.kind, t.text.c_str(), t.numberValue, t.location.line, t.location.column);
    }
}

void Lexer::skipWhitespace()
{
    while (idx_ < src_.size())
    {
        char c = src_[idx_];

        if (isspace((unsigned char)c))
        {
            advancePosition(c);
            ++idx_;
            continue;
        }

        if (c == '/' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '/')
        {

            advancePosition(src_[idx_]);
            ++idx_;
            advancePosition(src_[idx_]);
            ++idx_;

            while (idx_ < src_.size() && src_[idx_] != '\n')
            {
                advancePosition(src_[idx_]);
                ++idx_;
            }
            if (idx_ < src_.size() && src_[idx_] == '\n')
            {
                advancePosition(src_[idx_]);
                ++idx_;
            }
            continue;
        }

        if (c == '/' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '*')
        {

            advancePosition(src_[idx_]);
            ++idx_;
            advancePosition(src_[idx_]);
            ++idx_;

            while (idx_ + 1 < src_.size())
            {
                char cc = src_[idx_];
                if (cc == '*' && src_[idx_ + 1] == '/')
                {

                    advancePosition(src_[idx_]);
                    ++idx_;
                    advancePosition(src_[idx_]);
                    ++idx_;
                    break;
                }
                advancePosition(cc);
                ++idx_;
            }
            continue;
        }
        break;
    }
}

void Lexer::advancePosition(char c)
{
    if (c == '\n')
    {
        currentLocation_.line++;
        currentLocation_.column = 1;
    }
    else
    {
        currentLocation_.column++;
    }
}

Token Lexer::next()
{
    skipWhitespace();
    if (idx_ >= src_.size())
    {
        Token token{tok_eof, 0.0, ""};
        token.location = currentLocation_;
        return token;
    }

    SourceLocation tokenStart = currentLocation_;
    char c = src_[idx_];
    if (isalpha((unsigned char)c) || c == '_')
    {
        size_t start = idx_;
        advancePosition(c);
        ++idx_;
        while (idx_ < src_.size() && (isalnum((unsigned char)src_[idx_]) || src_[idx_] == '_'))
        {
            advancePosition(src_[idx_]);
            ++idx_;
        }
        std::string s = src_.substr(start, idx_ - start);
        Token tok;
        tok.location = tokenStart;
        tok.text = s;
        tok.numberValue = 0.0;

        if (s == "import")
        {
            tok.kind = tok_include;
            if (verbose_)
                printf("[lexer] import directive at %d:%d\n", tokenStart.line, tokenStart.column);
            return tok;
        }

        if (s == "module")
        {
            tok.kind = tok_module;
            if (verbose_)
                printf("[lexer] module declaration at %d:%d\n", tokenStart.line, tokenStart.column);
            return tok;
        }

        if (s == "fn")
        {
            tok.kind = tok_fn;
            if (verbose_)
                printf("[lexer] fn keyword at %d:%d\n", tokenStart.line, tokenStart.column);
            return tok;
        }

        if (s == "in")
        {
            tok.kind = tok_in;
            if (verbose_)
                printf("[lexer] in at %d:%d\n", tokenStart.line, tokenStart.column);
        }
        else if (s == "if")
            tok.kind = tok_if;
        else if (s == "elif")
            tok.kind = tok_elif;
        else if (s == "else")
            tok.kind = tok_else;
        else if (s == "while")
            tok.kind = tok_while;
        else if (s == "ret")
            tok.kind = tok_ret;
        else if (s == "true")
        {
            tok.kind = tok_true;
            tok.numberValue = 1.0;
        }
        else if (s == "false")
            tok.kind = tok_false;
        else if (s == "bool")
            tok.kind = tok_bool;
        else if (s == "int")
            tok.kind = tok_int;
        else if (s == "str")
            tok.kind = tok_str;
        else if (s == "float")
            tok.kind = tok_float;
        else if (s == "double")
            tok.kind = tok_double;
        else if (s == "char")
            tok.kind = tok_char;
        else if (s == "extern")
            tok.kind = tok_extern;
        else if (s == "var")
            tok.kind = tok_var;
        else if (s == "struct")
            tok.kind = tok_struct;
        else if (s == "impl")
            tok.kind = tok_impl;
        else if (s == "data")
            tok.kind = tok_data;
        else if (s == "extend")
            tok.kind = tok_extend;
        else if (s == "this")
            tok.kind = tok_this;
        else if (s == "match")
            tok.kind = tok_match;
        else if (s == "break")
            tok.kind = tok_break;
        else if (s == "continue")
            tok.kind = tok_continue;
        else if (s == "null")
            tok.kind = tok_null;
        else if (s == "void")
            tok.kind = tok_void;
        else if (s == "map")
            tok.kind = tok_map;
        else if (s == "list")
            tok.kind = tok_list;
        else
        {
            tok.kind = tok_identifier;
            if (verbose_)
                printf("[lexer] identifier '%s' at %d:%d\n", s.c_str(), tokenStart.line, tokenStart.column);
        }
        return tok;
    }
    if (isdigit((unsigned char)c))
    {
        size_t start = idx_;
        bool seenDot = false;

        if (src_[idx_] == '0' && idx_ + 1 < src_.size() && (src_[idx_ + 1] == 'x' || src_[idx_ + 1] == 'X'))
        {

            advancePosition(src_[idx_]);
            ++idx_;
            advancePosition(src_[idx_]);
            ++idx_;
            size_t hexStart = idx_;
            while (idx_ < src_.size())
            {
                char ch = src_[idx_];
                if (isxdigit((unsigned char)ch))
                {
                    advancePosition(ch);
                    ++idx_;
                    continue;
                }
                break;
            }
            std::string hexDigits = src_.substr(hexStart, idx_ - hexStart);
            unsigned long long val = 0ULL;
            if (!hexDigits.empty())
            {
                for (char hc : hexDigits)
                {
                    unsigned d;
                    if (hc >= '0' && hc <= '9')
                        d = hc - '0';
                    else if (hc >= 'a' && hc <= 'f')
                        d = 10 + (hc - 'a');
                    else if (hc >= 'A' && hc <= 'F')
                        d = 10 + (hc - 'A');
                    else
                        break;
                    val = (val << 4) | d;
                }
            }
            Token tok{tok_number, static_cast<double>(val), ""};
            tok.location = tokenStart;
            tok.text = std::string("0x") + hexDigits;
            if (verbose_)
                printf("[lexer] hex number 0x%s (%llu) at %d:%d\n", hexDigits.c_str(), val, tokenStart.line, tokenStart.column);
            return tok;
        }
        while (idx_ < src_.size())
        {
            char ch = src_[idx_];
            if (isdigit((unsigned char)ch))
            {
                advancePosition(ch);
                ++idx_;
                continue;
            }
            if (ch == '.')
            {
                if (idx_ + 1 < src_.size() && src_[idx_ + 1] == '.')
                {
                    break;
                }
                if (seenDot)
                {
                    break;
                }
                seenDot = true;
                advancePosition(ch);
                ++idx_;
                continue;
            }
            break;
        }
        std::string lexeme = src_.substr(start, idx_ - start);
        double v = std::stod(lexeme);

        bool isFloatLiteral = false;
        if (idx_ < src_.size() && (src_[idx_] == 'f' || src_[idx_] == 'F'))
        {
            isFloatLiteral = true;
            advancePosition(src_[idx_]);
            ++idx_;
        }

        Token tok{isFloatLiteral ? tok_float_literal : tok_number, v, ""};
        tok.location = tokenStart;
        tok.text = lexeme + (isFloatLiteral ? "f" : "");
        if (verbose_)
            printf("[lexer] %s %s (%g) at %d:%d\n", isFloatLiteral ? "float" : "number", lexeme.c_str(), v, tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '"')
    {
        advancePosition(c);
        ++idx_;
        std::string s;
        while (idx_ < src_.size() && src_[idx_] != '"')
        {
            char ch = src_[idx_];
            if (ch == '\\' && idx_ + 1 < src_.size())
            {

                advancePosition(ch);
                ++idx_;
                ch = src_[idx_];
                switch (ch)
                {
                case 'n':
                    s += '\n';
                    break;
                case 't':
                    s += '\t';
                    break;
                case 'r':
                    s += '\r';
                    break;
                case 'b':
                    s += '\b';
                    break;
                case 'f':
                    s += '\f';
                    break;
                case 'a':
                    s += '\a';
                    break;
                case 'v':
                    s += '\v';
                    break;
                case '\\':
                    s += '\\';
                    break;
                case '"':
                    s += '"';
                    break;
                case '\'':
                    s += '\'';
                    break;
                case '0':
                    s += '\0';
                    break;
                default:
                    s += '\\';
                    s += ch;
                    break;
                }
            }
            else
            {
                s += ch;
            }
            advancePosition(src_[idx_]);
            ++idx_;
        }
        if (idx_ < src_.size() && src_[idx_] == '"')
        {
            advancePosition(src_[idx_]);
            ++idx_;
        }
        Token tok{tok_string, 0.0, s};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] string '%s' at %d:%d\n", s.c_str(), tokenStart.line, tokenStart.column);
        return tok;
    }

    if (c == '\'')
    {
        advancePosition(c);
        ++idx_;
        char charValue = 0;
        if (idx_ < src_.size())
        {
            char ch = src_[idx_];
            if (ch == '\\' && idx_ + 1 < src_.size())
            {

                advancePosition(ch);
                ++idx_;
                ch = src_[idx_];
                switch (ch)
                {
                case 'n':
                    charValue = '\n';
                    break;
                case 't':
                    charValue = '\t';
                    break;
                case 'r':
                    charValue = '\r';
                    break;
                case 'b':
                    charValue = '\b';
                    break;
                case 'f':
                    charValue = '\f';
                    break;
                case 'a':
                    charValue = '\a';
                    break;
                case 'v':
                    charValue = '\v';
                    break;
                case '\\':
                    charValue = '\\';
                    break;
                case '\'':
                    charValue = '\'';
                    break;
                case '"':
                    charValue = '"';
                    break;
                case '0':
                    charValue = '\0';
                    break;
                default:
                    charValue = ch;
                    break;
                }
            }
            else
            {
                charValue = ch;
            }
            advancePosition(src_[idx_]);
            ++idx_;
        }

        if (idx_ < src_.size() && src_[idx_] == '\'')
        {
            advancePosition(src_[idx_]);
            ++idx_;
        }
        Token tok{tok_char_literal, static_cast<double>(charValue), std::string(1, charValue)};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] char '%c' (%d) at %d:%d\n", charValue, (int)charValue, tokenStart.line, tokenStart.column);
        return tok;
    }

    if (c == '.' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '.')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_range, 0.0, ".."};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] .. at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '&' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '&')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_and, 0.0, "&&"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] && at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }

    if (c == '&' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '=')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_bitwise_and_eq, 0.0, "&="};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] &= at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }

    if (c == '&')
    {
        advancePosition(c);
        ++idx_;
        Token tok{tok_ampersand, 0.0, "&"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] & at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '|' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '|')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_or, 0.0, "||"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] || at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }

    if (c == '|' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '=')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_bitwise_or_eq, 0.0, "|="};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] |= at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }

    if (c == '|')
    {
        advancePosition(c);
        ++idx_;
        Token tok{tok_bitwise_or, 0.0, "|"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] | at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }

    if (c == '^' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '=')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_bitwise_xor_eq, 0.0, "^="};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] ^= at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }

    if (c == '^')
    {
        advancePosition(c);
        ++idx_;
        Token tok{tok_bitwise_xor, 0.0, "^"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] ^ at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }

    if (c == '~')
    {
        advancePosition(c);
        ++idx_;
        Token tok{tok_bitwise_not, 0.0, "~"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] ~ at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '=' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '>')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_fat_arrow, 0.0, "=>"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] => at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '=' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '=')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_eq, 0.0, "=="};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] == at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '!' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '=')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_ne, 0.0, "!="};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] != at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '-' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '>')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_arrow, 0.0, "->"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] -> at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '-' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '=')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_minus_eq, 0.0, "-="};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] -= at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }

    if (c == '<' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '<')
    {
        if (idx_ + 2 < src_.size() && src_[idx_ + 2] == '=')
        {
            advancePosition(c);
            ++idx_;
            advancePosition(src_[idx_]);
            ++idx_;
            advancePosition(src_[idx_]);
            ++idx_;
            Token tok{tok_shift_left_eq, 0.0, "<<="};
            tok.location = tokenStart;
            if (verbose_)
                printf("[lexer] <<= at %d:%d\n", tokenStart.line, tokenStart.column);
            return tok;
        }

        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_shift_left, 0.0, "<<"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] << at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }

    if (c == '>' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '>')
    {
        if (idx_ + 2 < src_.size() && src_[idx_ + 2] == '=')
        {
            advancePosition(c);
            ++idx_;
            advancePosition(src_[idx_]);
            ++idx_;
            advancePosition(src_[idx_]);
            ++idx_;
            Token tok{tok_shift_right_eq, 0.0, ">>="};
            tok.location = tokenStart;
            if (verbose_)
                printf("[lexer] >>= at %d:%d\n", tokenStart.line, tokenStart.column);
            return tok;
        }

        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_shift_right, 0.0, ">>"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] >> at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '<' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '=')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_le, 0.0, "<="};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] <= at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '>' && idx_ + 1 < src_.size() && src_[idx_ + 1] == '=')
    {
        advancePosition(c);
        ++idx_;
        advancePosition(src_[idx_]);
        ++idx_;
        Token tok{tok_ge, 0.0, ">="};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] >= at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '<')
    {
        advancePosition(c);
        ++idx_;
        Token tok{tok_lt, 0.0, "<"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] < at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '>')
    {
        advancePosition(c);
        ++idx_;
        Token tok{tok_gt, 0.0, ">"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] > at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }
    if (c == '!')
    {
        advancePosition(c);
        ++idx_;
        Token tok{tok_not, 0.0, "!"};
        tok.location = tokenStart;
        if (verbose_)
            printf("[lexer] ! at %d:%d\n", tokenStart.line, tokenStart.column);
        return tok;
    }

    advancePosition(c);
    ++idx_;
    Token tok;
    tok.location = tokenStart;
    tok.numberValue = 0.0;

    switch (c)
    {
    case ':':
        tok.kind = tok_colon;
        tok.text = ":";
        if (verbose_)
            printf("[lexer] : at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    case '{':
        tok.kind = tok_brace_open;
        tok.text = "{";
        if (verbose_)
            printf("[lexer] { at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    case '}':
        tok.kind = tok_brace_close;
        tok.text = "}";
        if (verbose_)
            printf("[lexer] } at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    case ',':
        tok.kind = tok_comma;
        tok.text = ",";
        if (verbose_)
            printf("[lexer] , at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    case '=':
        tok.kind = tok_equal;
        tok.text = "=";
        if (verbose_)
            printf("[lexer] = at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    case ';':
        tok.kind = tok_semicolon;
        tok.text = ";";
        if (verbose_)
            printf("[lexer] ; at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    case '(':
        tok.kind = tok_paren_open;
        tok.text = "(";
        if (verbose_)
            printf("[lexer] ( at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    case ')':
        tok.kind = tok_paren_close;
        tok.text = ")";
        if (verbose_)
            printf("[lexer] ) at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    case '+':
        if (idx_ < src_.size() && src_[idx_] == '=')
        {
            advancePosition(src_[idx_]);
            ++idx_;
            tok.kind = tok_plus_eq;
            tok.text = "+=";
            if (verbose_)
                printf("[lexer] += at %d:%d\n", tokenStart.line, tokenStart.column);
        }
        else
        {
            tok.kind = tok_plus;
            tok.text = "+";
            if (verbose_)
                printf("[lexer] + at %d:%d\n", tokenStart.line, tokenStart.column);
        }
        break;
    case '-':
        tok.kind = tok_minus;
        tok.text = "-";
        if (verbose_)
            printf("[lexer] - at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    case '*':
        if (idx_ < src_.size() && src_[idx_] == '=')
        {
            advancePosition(src_[idx_]);
            ++idx_;
            tok.kind = tok_mul_eq;
            tok.text = "*=";
            if (verbose_)
                printf("[lexer] *= at %d:%d\n", tokenStart.line, tokenStart.column);
        }
        else
        {
            tok.kind = tok_mul;
            tok.text = "*";
            if (verbose_)
                printf("[lexer] * at %d:%d\n", tokenStart.line, tokenStart.column);
        }
        break;
    case '/':
        if (idx_ < src_.size() && src_[idx_] == '=')
        {
            advancePosition(src_[idx_]);
            ++idx_;
            tok.kind = tok_div_eq;
            tok.text = "/=";
            if (verbose_)
                printf("[lexer] /= at %d:%d\n", tokenStart.line, tokenStart.column);
        }
        else
        {
            tok.kind = tok_div;
            tok.text = "/";
            if (verbose_)
                printf("[lexer] / at %d:%d\n", tokenStart.line, tokenStart.column);
        }
        break;
    case '%':
        if (idx_ < src_.size() && src_[idx_] == '=')
        {
            advancePosition(src_[idx_]);
            ++idx_;
            tok.kind = tok_mod_eq;
            tok.text = "%=";
            if (verbose_)
                printf("[lexer] %%= at %d:%d\n", tokenStart.line, tokenStart.column);
        }
        else
        {
            tok.kind = tok_mod;
            tok.text = "%";
            if (verbose_)
                printf("[lexer] %% at %d:%d\n", tokenStart.line, tokenStart.column);
        }
        break;
    case '.':
        tok.kind = tok_dot;
        tok.text = ".";
        if (verbose_)
            printf("[lexer] . at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    case '[':
        tok.kind = tok_square_bracket_open;
        tok.text = "[";
        if (verbose_)
            printf("[lexer] [ at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    case ']':
        tok.kind = tok_square_bracket_close;
        tok.text = "]";
        if (verbose_)
            printf("[lexer] ] at %d:%d\n", tokenStart.line, tokenStart.column);
        break;
    default:
        tok.kind = tok_unknown;
        tok.text = std::string(1, c);
        if (verbose_)
            printf("[lexer] unknown '%c' at %d:%d\n", c, tokenStart.line, tokenStart.column);
        break;
    }
    return tok;
}

Token Lexer::peek()
{
    return cur_;
}
