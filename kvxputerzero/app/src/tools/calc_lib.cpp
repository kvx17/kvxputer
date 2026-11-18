#include "calc_lib.h"

#include "user_paths.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

namespace tools::calc {
namespace {

constexpr int kHistCap = 8;

enum FuncId : uint8_t {
    F_SIN,
    F_COS,
    F_TAN,
    F_ASIN,
    F_ACOS,
    F_ATAN,
    F_LN,
    F_LOG,
    F_LOG2,
    F_EXP,
    F_ABS,
    F_SQRT,
    F_FACT,
};

enum IdentId : uint8_t { I_PI, I_E, I_ANS };

enum FuncKind : uint8_t { FK_TRIG, FK_INVTRIG, FK_RAW };

struct Token {
    enum Type : uint8_t { NUMBER, OP, LPAREN, RPAREN, FUNC, IDENT } type;
    double value = 0;
    char op = 0;
    FuncId func = F_SIN;
    IdentId ident = I_PI;
};

struct NameEntry {
    const char* name;
    bool is_func;
    uint8_t id;
};

const NameEntry kNames[] = {
    {"asin", true, F_ASIN}, {"acos", true, F_ACOS}, {"atan", true, F_ATAN}, {"log2", true, F_LOG2},
    {"sqrt", true, F_SQRT}, {"fact", true, F_FACT}, {"sin", true, F_SIN},   {"cos", true, F_COS},
    {"tan", true, F_TAN},   {"log", true, F_LOG},   {"exp", true, F_EXP},   {"abs", true, F_ABS},
    {"ln", true, F_LN},     {"ans", false, I_ANS},  {"pi", false, I_PI},    {"e", false, I_E},
};

bool eq_ignore_case(const char* a, const char* b, int len) {
    for (int i = 0; i < len; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool match_name(const std::string& expr, int pos, const NameEntry*& out) {
    const int n = static_cast<int>(expr.size());
    for (const auto& e : kNames) {
        const int len = static_cast<int>(std::strlen(e.name));
        if (pos + len > n) continue;
        if (!eq_ignore_case(expr.c_str() + pos, e.name, len)) continue;
        const char next = (pos + len < n) ? expr[static_cast<size_t>(pos + len)] : '\0';
        if (std::isalpha(static_cast<unsigned char>(next)) || next == '_') continue;
        out = &e;
        return true;
    }
    return false;
}

FuncKind func_kind(FuncId id) {
    switch (id) {
    case F_SIN:
    case F_COS:
    case F_TAN:
        return FK_TRIG;
    case F_ASIN:
    case F_ACOS:
    case F_ATAN:
        return FK_INVTRIG;
    default:
        return FK_RAW;
    }
}

bool tokenize(const std::string& expr, std::vector<Token>& tokens) {
    tokens.clear();
    const int n = static_cast<int>(expr.size());
    int i = 0;

    auto is_operator_context = [&]() {
        if (tokens.empty()) return true;
        const auto& last = tokens.back();
        return last.type == Token::OP || last.type == Token::LPAREN || last.type == Token::FUNC;
    };

    auto maybe_implicit = [&](Token::Type next) {
        if (tokens.empty()) return;
        const auto& last = tokens.back();
        const bool left_ok =
            last.type == Token::NUMBER || last.type == Token::IDENT || last.type == Token::RPAREN;
        const bool right_ok =
            next == Token::NUMBER || next == Token::IDENT || next == Token::LPAREN || next == Token::FUNC;
        if (left_ok && right_ok) {
            Token mul;
            mul.type = Token::OP;
            mul.op = '*';
            tokens.push_back(mul);
        }
    };

    while (i < n) {
        const char c = expr[static_cast<size_t>(i)];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            const int start = i;
            bool dot = false;
            while (i < n && (std::isdigit(static_cast<unsigned char>(expr[static_cast<size_t>(i)])) ||
                             expr[static_cast<size_t>(i)] == '.')) {
                if (expr[static_cast<size_t>(i)] == '.') {
                    if (dot) return false;
                    dot = true;
                }
                ++i;
            }
            if (i == start || (i - start == 1 && expr[static_cast<size_t>(start)] == '.')) return false;
            if (i < n && (expr[static_cast<size_t>(i)] == 'e' || expr[static_cast<size_t>(i)] == 'E')) {
                const int epos = i;
                ++i;
                if (i < n && (expr[static_cast<size_t>(i)] == '+' || expr[static_cast<size_t>(i)] == '-'))
                    ++i;
                const int dig_start = i;
                while (i < n && std::isdigit(static_cast<unsigned char>(expr[static_cast<size_t>(i)]))) ++i;
                if (i == dig_start) i = epos;
            }
            maybe_implicit(Token::NUMBER);
            Token t;
            t.type = Token::NUMBER;
            t.value = std::strtod(expr.c_str() + start, nullptr);
            tokens.push_back(t);
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
            const NameEntry* e = nullptr;
            if (!match_name(expr, i, e)) return false;
            const int len = static_cast<int>(std::strlen(e->name));
            if (e->is_func) {
                maybe_implicit(Token::FUNC);
                Token t;
                t.type = Token::FUNC;
                t.func = static_cast<FuncId>(e->id);
                tokens.push_back(t);
            } else {
                maybe_implicit(Token::IDENT);
                Token t;
                t.type = Token::IDENT;
                t.ident = static_cast<IdentId>(e->id);
                tokens.push_back(t);
            }
            i += len;
            continue;
        }
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^') {
            Token t;
            t.type = Token::OP;
            if (c == '-' && is_operator_context()) {
                t.op = 'm';
            } else if (c == '+' && is_operator_context()) {
                ++i;
                continue;
            } else {
                t.op = c;
            }
            tokens.push_back(t);
            ++i;
            continue;
        }
        if (c == '(') {
            maybe_implicit(Token::LPAREN);
            Token t;
            t.type = Token::LPAREN;
            tokens.push_back(t);
            ++i;
            continue;
        }
        if (c == ')') {
            Token t;
            t.type = Token::RPAREN;
            tokens.push_back(t);
            ++i;
            continue;
        }
        return false;
    }
    return true;
}

int precedence(char op) {
    if (op == '^') return 5;
    if (op == 'm') return 4;
    if (op == '*' || op == '/' || op == '%') return 3;
    if (op == '+' || op == '-') return 2;
    return 0;
}

bool right_associative(char op) {
    return op == 'm' || op == '^';
}

bool apply_func(FuncId id, double arg, bool deg_mode, double& out) {
    const FuncKind kind = func_kind(id);
    double a = arg;
    if (kind == FK_TRIG && deg_mode) a = a * M_PI / 180.0;

    switch (id) {
    case F_SIN:
        out = std::sin(a);
        break;
    case F_COS:
        out = std::cos(a);
        break;
    case F_TAN:
        out = std::tan(a);
        break;
    case F_ASIN:
        if (arg < -1.0 || arg > 1.0) return false;
        out = std::asin(arg);
        break;
    case F_ACOS:
        if (arg < -1.0 || arg > 1.0) return false;
        out = std::acos(arg);
        break;
    case F_ATAN:
        out = std::atan(arg);
        break;
    case F_LN:
        if (arg <= 0) return false;
        out = std::log(arg);
        break;
    case F_LOG:
        if (arg <= 0) return false;
        out = std::log10(arg);
        break;
    case F_LOG2:
        if (arg <= 0) return false;
        out = std::log2(arg);
        break;
    case F_EXP:
        out = std::exp(arg);
        break;
    case F_ABS:
        out = std::fabs(arg);
        break;
    case F_SQRT:
        if (arg < 0) return false;
        out = std::sqrt(arg);
        break;
    case F_FACT: {
        if (arg < 0 || std::fabs(arg - std::round(arg)) > 1e-9) return false;
        const long n = static_cast<long>(std::round(arg));
        if (n > 170) return false;
        double f = 1.0;
        for (long k = 2; k <= n; ++k) f *= static_cast<double>(k);
        out = f;
        break;
    }
    default:
        return false;
    }

    if (kind == FK_INVTRIG && deg_mode) out = out * 180.0 / M_PI;
    if (std::isnan(out) || std::isinf(out)) return false;
    return true;
}

bool evaluate_tokens(const std::vector<Token>& tokens, const State& state, double& result) {
    if (tokens.empty()) return false;

    std::vector<Token> output;
    std::vector<Token> stack;

    for (const Token& t : tokens) {
        switch (t.type) {
        case Token::NUMBER:
        case Token::IDENT:
            output.push_back(t);
            break;
        case Token::FUNC:
            stack.push_back(t);
            break;
        case Token::OP:
            while (!stack.empty()) {
                const Token& top = stack.back();
                if (top.type == Token::FUNC) {
                    output.push_back(top);
                    stack.pop_back();
                    continue;
                }
                if (top.type != Token::OP) break;
                if ((right_associative(t.op) && precedence(t.op) < precedence(top.op)) ||
                    (!right_associative(t.op) && precedence(t.op) <= precedence(top.op))) {
                    output.push_back(top);
                    stack.pop_back();
                } else {
                    break;
                }
            }
            stack.push_back(t);
            break;
        case Token::LPAREN:
            stack.push_back(t);
            break;
        case Token::RPAREN:
            while (!stack.empty() && stack.back().type != Token::LPAREN) {
                output.push_back(stack.back());
                stack.pop_back();
            }
            if (stack.empty()) return false;
            stack.pop_back();
            while (!stack.empty() && stack.back().type == Token::FUNC) {
                output.push_back(stack.back());
                stack.pop_back();
            }
            break;
        }
    }
    while (!stack.empty()) {
        if (stack.back().type == Token::LPAREN) return false;
        output.push_back(stack.back());
        stack.pop_back();
    }

    std::vector<double> eval;
    for (const Token& t : output) {
        if (t.type == Token::NUMBER) {
            eval.push_back(t.value);
        } else if (t.type == Token::IDENT) {
            switch (t.ident) {
            case I_PI:
                eval.push_back(M_PI);
                break;
            case I_E:
                eval.push_back(M_E);
                break;
            case I_ANS:
                eval.push_back(state.has_ans ? state.ans : 0.0);
                break;
            default:
                return false;
            }
        } else if (t.type == Token::FUNC) {
            if (eval.empty()) return false;
            const double a = eval.back();
            eval.pop_back();
            double r = 0;
            if (!apply_func(t.func, a, state.deg_mode, r)) return false;
            eval.push_back(r);
        } else if (t.type == Token::OP) {
            if (t.op == 'm') {
                if (eval.empty()) return false;
                const double a = eval.back();
                eval.pop_back();
                eval.push_back(-a);
            } else {
                if (eval.size() < 2) return false;
                const double b = eval.back();
                eval.pop_back();
                const double a = eval.back();
                eval.pop_back();
                double r = 0;
                switch (t.op) {
                case '+':
                    r = a + b;
                    break;
                case '-':
                    r = a - b;
                    break;
                case '*':
                    r = a * b;
                    break;
                case '/':
                    if (b == 0) return false;
                    r = a / b;
                    break;
                case '%':
                    if (b == 0) return false;
                    r = std::fmod(a, b);
                    break;
                case '^':
                    r = std::pow(a, b);
                    break;
                default:
                    return false;
                }
                if (std::isnan(r) || std::isinf(r)) return false;
                eval.push_back(r);
            }
        }
    }
    if (eval.size() != 1) return false;
    if (std::isnan(eval[0]) || std::isinf(eval[0])) return false;
    result = eval[0];
    return true;
}

} // namespace

std::string format_result(double value) {
    char buf[48];
    if (value == static_cast<long long>(value) && std::fabs(value) < 1e15) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
    } else {
        std::snprintf(buf, sizeof(buf), "%.6g", value);
    }
    return buf;
}

bool evaluate(const std::string& expr, State& state, double& result, std::string& error) {
    std::string cleaned;
    cleaned.reserve(expr.size());
    for (char c : expr) {
        if (!std::isspace(static_cast<unsigned char>(c))) cleaned.push_back(c);
    }
    if (cleaned.empty()) {
        error = "empty";
        return false;
    }
    std::vector<Token> tokens;
    if (!tokenize(cleaned, tokens)) {
        error = "syntax";
        return false;
    }
    if (!evaluate_tokens(tokens, state, result)) {
        error = "error";
        return false;
    }
    state.ans = result;
    state.has_ans = true;
    std::string line = cleaned + " = " + format_result(result);
    state.history.insert(state.history.begin(), std::move(line));
    if (static_cast<int>(state.history.size()) > kHistCap) state.history.resize(static_cast<size_t>(kHistCap));
    return true;
}

void load_prefs(State& state) {
    const std::string path = tools::config_file("calc.conf");
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("deg_mode=", 0) == 0) {
            const std::string v = line.substr(9);
            state.deg_mode = !(v == "0" || v == "no" || v == "false" || v == "rad");
        }
    }
}

void save_prefs(const State& state) {
    const std::string path = tools::config_file("calc.conf");
    std::ofstream out(path);
    if (!out) return;
    out << "deg_mode=" << (state.deg_mode ? "1" : "0") << "\n";
}

} // namespace tools::calc
