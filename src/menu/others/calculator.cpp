/*
 * Scientific calculator for kvxputer Tools / PDA.
 * Ideas adapted from flamyez/adv_calc (MIT) and the Cardulator function set;
 * no third-party code copied.
 */
#include "calculator.h"

#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <cctype>
#include <cstring>
#include <globals.h>
#include <math.h>
#include <vector>

#ifndef LITE_VERSION
#include "menu/others/pda/pda_alarms.h"
#endif

namespace {

static constexpr int kMaxExpr = 96;
static constexpr int kHistCap = 8;
static constexpr unsigned long kBlinkMs = 400;

static bool gDegMode = true;
static double gAns = 0.0;
static bool gHasAns = false;

// ---- Tokens ----------------------------------------------------------------

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

enum IdentId : uint8_t { I_PI, I_E, I_ANS, I_X };

enum FuncKind : uint8_t { FK_TRIG, FK_INVTRIG, FK_RAW };

struct Token {
    enum Type : uint8_t { NUMBER, OP, LPAREN, RPAREN, FUNC, IDENT } type;
    double value = 0; // NUMBER
    char op = 0;      // OP: + - * / % ^ m
    FuncId func = F_SIN;
    IdentId ident = I_PI;
};

struct NameEntry {
    const char *name;
    bool isFunc;
    uint8_t id; // FuncId or IdentId
};

// Longer names first so "log2" / "asin" win over "log" / "a".
static const NameEntry kNames[] = {
    {"asin", true,  F_ASIN},
    {"acos", true,  F_ACOS},
    {"atan", true,  F_ATAN},
    {"log2", true,  F_LOG2},
    {"sqrt", true,  F_SQRT},
    {"fact", true,  F_FACT},
    {"sin",  true,  F_SIN },
    {"cos",  true,  F_COS },
    {"tan",  true,  F_TAN },
    {"log",  true,  F_LOG },
    {"exp",  true,  F_EXP },
    {"abs",  true,  F_ABS },
    {"ln",   true,  F_LN  },
    {"ans",  false, I_ANS },
    {"pi",   false, I_PI  },
    {"e",    false, I_E   },
    {"x",    false, I_X   },
};

static FuncKind funcKind(FuncId id) {
    switch (id) {
        case F_SIN:
        case F_COS:
        case F_TAN: return FK_TRIG;
        case F_ASIN:
        case F_ACOS:
        case F_ATAN: return FK_INVTRIG;
        default: return FK_RAW;
    }
}

static bool eqIgnoreCase(const char *a, const char *b, int len) {
    for (int i = 0; i < len; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    }
    return true;
}

static bool matchName(const String &expr, int pos, const NameEntry *&out) {
    int n = expr.length();
    for (const NameEntry &e : kNames) {
        int len = (int)strlen(e.name);
        if (pos + len > n) continue;
        if (!eqIgnoreCase(expr.c_str() + pos, e.name, len)) continue;
        char next = (pos + len < n) ? expr[pos + len] : '\0';
        if (isalpha((unsigned char)next) || next == '_') continue;
        out = &e;
        return true;
    }
    return false;
}

// True if a side of '=' contains the variable x (not buried in another name).
static bool sideHasX(const String &s) {
    int i = 0;
    int n = s.length();
    while (i < n) {
        char c = s[i];
        if (isalpha((unsigned char)c)) {
            const NameEntry *e = nullptr;
            if (matchName(s, i, e)) {
                if (!e->isFunc && e->id == I_X) return true;
                i += (int)strlen(e->name);
            } else {
                // Skip unknown identifier letters.
                while (i < n && isalpha((unsigned char)s[i])) i++;
            }
        } else {
            i++;
        }
    }
    return false;
}

static bool needsImplicitMul(const Token &prev) {
    if (prev.type == Token::NUMBER) return true;
    if (prev.type == Token::RPAREN) return true;
    if (prev.type == Token::IDENT) return true;
    // FUNC followed by '(' is the call; FUNC then IDENT/NUMBER is rare but allow.
    return false;
}

static bool tokenize(const String &expr, std::vector<Token> &out) {
    out.clear();
    int i = 0;
    int n = expr.length();
    Token prev;
    bool hasPrev = false;

    auto isOperatorContext = [&]() {
        if (!hasPrev) return true;
        if (prev.type == Token::OP) return true;
        if (prev.type == Token::LPAREN) return true;
        return false;
    };

    auto pushTok = [&](const Token &t) {
        out.push_back(t);
        prev = t;
        hasPrev = true;
    };

    auto maybeImplicit = [&](Token::Type nextType) {
        if (!hasPrev) return;
        if (!needsImplicitMul(prev)) return;
        if (nextType != Token::NUMBER && nextType != Token::IDENT && nextType != Token::LPAREN &&
            nextType != Token::FUNC)
            return;
        Token mul;
        mul.type = Token::OP;
        mul.op = '*';
        pushTok(mul);
    };

    while (i < n) {
        char c = expr[i];
        if (c == ' ') {
            i++;
            continue;
        }

        // Number (optional leading digits/dot) + optional scientific exponent.
        if (isdigit((unsigned char)c) || c == '.') {
            int start = i;
            bool dot = false;
            while (i < n && (isdigit((unsigned char)expr[i]) || expr[i] == '.')) {
                if (expr[i] == '.') {
                    if (dot) return false;
                    dot = true;
                }
                i++;
            }
            if (i == start || (i - start == 1 && expr[start] == '.')) return false;
            // Scientific: 1e-3 / 2.5E+4
            if (i < n && (expr[i] == 'e' || expr[i] == 'E')) {
                int ePos = i;
                i++;
                if (i < n && (expr[i] == '+' || expr[i] == '-')) i++;
                int digStart = i;
                while (i < n && isdigit((unsigned char)expr[i])) i++;
                if (i == digStart) {
                    // Not scientific — leave 'e' for identifier pass (rewind).
                    i = ePos;
                }
            }
            String num = expr.substring(start, i);
            maybeImplicit(Token::NUMBER);
            Token t;
            t.type = Token::NUMBER;
            t.value = num.toDouble();
            pushTok(t);
            continue;
        }

        // Identifiers / functions
        if (isalpha((unsigned char)c)) {
            const NameEntry *e = nullptr;
            if (!matchName(expr, i, e)) return false;
            int len = (int)strlen(e->name);
            if (e->isFunc) {
                maybeImplicit(Token::FUNC);
                Token t;
                t.type = Token::FUNC;
                t.func = (FuncId)e->id;
                pushTok(t);
            } else {
                maybeImplicit(Token::IDENT);
                Token t;
                t.type = Token::IDENT;
                t.ident = (IdentId)e->id;
                pushTok(t);
            }
            i += len;
            continue;
        }

        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^') {
            Token t;
            t.type = Token::OP;
            if (c == '-' && isOperatorContext()) {
                t.op = 'm';
            } else if (c == '+' && isOperatorContext()) {
                i++;
                continue;
            } else {
                t.op = c;
            }
            pushTok(t);
            i++;
            continue;
        }

        if (c == '(') {
            maybeImplicit(Token::LPAREN);
            Token t;
            t.type = Token::LPAREN;
            pushTok(t);
            i++;
            continue;
        }
        if (c == ')') {
            Token t;
            t.type = Token::RPAREN;
            pushTok(t);
            i++;
            continue;
        }
        return false;
    }
    return true;
}

static int precedence(char op) {
    if (op == '^') return 5;
    if (op == 'm') return 4;
    if (op == '*' || op == '/' || op == '%') return 3;
    if (op == '+' || op == '-') return 2;
    return 0;
}

static bool rightAssociative(char op) { return op == 'm' || op == '^'; }

struct EvalCtx {
    double xValue = 0;
    bool xBound = false;
};

static bool applyFunc(FuncId id, double arg, double &out) {
    FuncKind kind = funcKind(id);
    double a = arg;
    if (kind == FK_TRIG && gDegMode) a = a * M_PI / 180.0;

    switch (id) {
        case F_SIN: out = sin(a); break;
        case F_COS: out = cos(a); break;
        case F_TAN: out = tan(a); break;
        case F_ASIN:
            if (arg < -1.0 || arg > 1.0) return false;
            out = asin(arg);
            break;
        case F_ACOS:
            if (arg < -1.0 || arg > 1.0) return false;
            out = acos(arg);
            break;
        case F_ATAN: out = atan(arg); break;
        case F_LN:
            if (arg <= 0) return false;
            out = log(arg);
            break;
        case F_LOG:
            if (arg <= 0) return false;
            out = log10(arg);
            break;
        case F_LOG2:
            if (arg <= 0) return false;
            out = log2(arg);
            break;
        case F_EXP: out = exp(arg); break;
        case F_ABS: out = fabs(arg); break;
        case F_SQRT:
            if (arg < 0) return false;
            out = sqrt(arg);
            break;
        case F_FACT: {
            if (arg < 0 || fabs(arg - round(arg)) > 1e-9) return false;
            long n = (long)round(arg);
            if (n > 170) return false;
            double f = 1.0;
            for (long k = 2; k <= n; k++) f *= (double)k;
            out = f;
            break;
        }
        default: return false;
    }

    if (kind == FK_INVTRIG && gDegMode) out = out * 180.0 / M_PI;
    if (isnan(out) || isinf(out)) return false;
    return true;
}

static bool evaluateTokens(const std::vector<Token> &tokens, const EvalCtx &ctx, double &result) {
    if (tokens.empty()) return false;

    std::vector<Token> output;
    std::vector<Token> stack;

    for (const Token &t : tokens) {
        switch (t.type) {
            case Token::NUMBER:
            case Token::IDENT: output.push_back(t); break;
            case Token::FUNC:
                // Prefix function (precedence above ^); applied after its operand.
                stack.push_back(t);
                break;
            case Token::OP:
                while (!stack.empty()) {
                    const Token &top = stack.back();
                    if (top.type == Token::FUNC) {
                        output.push_back(top);
                        stack.pop_back();
                        continue;
                    }
                    if (top.type != Token::OP) break;
                    char topOp = top.op;
                    if ((rightAssociative(t.op) && precedence(t.op) < precedence(topOp)) ||
                        (!rightAssociative(t.op) && precedence(t.op) <= precedence(topOp))) {
                        output.push_back(top);
                        stack.pop_back();
                    } else break;
                }
                stack.push_back(t);
                break;
            case Token::LPAREN: stack.push_back(t); break;
            case Token::RPAREN:
                while (!stack.empty() && stack.back().type != Token::LPAREN) {
                    output.push_back(stack.back());
                    stack.pop_back();
                }
                if (stack.empty()) return false;
                stack.pop_back(); // '('
                // After '(...)', apply any pending FUNC (sin(...)).
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
    for (const Token &t : output) {
        if (t.type == Token::NUMBER) {
            eval.push_back(t.value);
        } else if (t.type == Token::IDENT) {
            switch (t.ident) {
                case I_PI: eval.push_back(M_PI); break;
                case I_E: eval.push_back(M_E); break;
                case I_ANS:
                    if (!gHasAns) eval.push_back(0.0);
                    else eval.push_back(gAns);
                    break;
                case I_X:
                    if (!ctx.xBound) return false;
                    eval.push_back(ctx.xValue);
                    break;
                default: return false;
            }
        } else if (t.type == Token::FUNC) {
            if (eval.empty()) return false;
            double a = eval.back();
            eval.pop_back();
            double r;
            if (!applyFunc(t.func, a, r)) return false;
            eval.push_back(r);
        } else if (t.type == Token::OP) {
            if (t.op == 'm') {
                if (eval.empty()) return false;
                double a = eval.back();
                eval.pop_back();
                eval.push_back(-a);
            } else {
                if (eval.size() < 2) return false;
                double b = eval.back();
                eval.pop_back();
                double a = eval.back();
                eval.pop_back();
                double r = 0;
                switch (t.op) {
                    case '+': r = a + b; break;
                    case '-': r = a - b; break;
                    case '*': r = a * b; break;
                    case '/':
                        if (b == 0) return false;
                        r = a / b;
                        break;
                    case '%':
                        if (b == 0) return false;
                        r = fmod(a, b);
                        break;
                    case '^': r = pow(a, b); break;
                    default: return false;
                }
                if (isnan(r) || isinf(r)) return false;
                eval.push_back(r);
            }
        }
    }
    if (eval.size() != 1) return false;
    if (isnan(eval[0]) || isinf(eval[0])) return false;
    result = eval[0];
    return true;
}

static bool evaluateExpr(const String &expr, const EvalCtx &ctx, double &result) {
    std::vector<Token> tokens;
    if (!tokenize(expr, tokens)) return false;
    return evaluateTokens(tokens, ctx, result);
}

static bool evaluateNumeric(const String &expr, double &result) {
    EvalCtx ctx;
    return evaluateExpr(expr, ctx, result);
}

static String formatResult(double value) {
    if (value == (long long)value && fabs(value) < 1e15) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)value);
        return String(buf);
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "%.6g", value);
    String s(buf);
    return s;
}

static String formatStatusNumeric(double value) {
    String body = formatResult(value);
    if (value == (long long)value && fabs(value) < 1e15) return "= " + body;
    return "=~ " + body;
}

static bool isBinaryOp(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^';
}

static bool shouldReplaceTrailingOp(const String &expr, int caret, char c) {
    if (caret <= 0 || caret > (int)expr.length()) return false;
    char prev = expr[caret - 1];
    if (!isBinaryOp(prev)) return false;
    if (c == '+' || c == '*' || c == '/' || c == '%' || c == '^') return true;
    if (c == '-') return prev == '+' || prev == '-';
    return false;
}

static void insertAtCaret(String &expr, int &caret, const String &ins) {
    if ((int)expr.length() + (int)ins.length() > kMaxExpr) return;
    if (caret < 0) caret = 0;
    if (caret > (int)expr.length()) caret = (int)expr.length();
    String tail = expr.substring(caret);
    expr.remove(caret);
    expr += ins;
    expr += tail;
    caret += (int)ins.length();
}

static void insertOrReplaceOp(String &expr, int &caret, char c) {
    if (shouldReplaceTrailingOp(expr, caret, c)) {
        expr.remove(caret - 1, 1);
        caret--;
    }
    char buf[2] = {c, 0};
    insertAtCaret(expr, caret, String(buf));
}

static void backspaceAt(String &expr, int &caret) {
    if (caret <= 0) return;
    caret--;
    expr.remove(caret, 1);
}

static void consumeNavFlags() {
    check(UpPress);
    check(DownPress);
    check(PrevPress);
    check(NextPress);
    check(SelPress);
    check(EscPress);
}

static bool isHidCmd(unsigned char c) {
    return c == 0xDA || c == 0xD9 || c == 0xD8 || c == 0xD7 || c == 0xB1 || c == 0xD4 || c == 0xB3;
}

// ---- Equation solve --------------------------------------------------------

enum SolveKind { SK_NUMERIC, SK_TRUE, SK_FALSE, SK_X, SK_ANY, SK_NONE, SK_LINEAR_ONLY, SK_ERROR };

struct SolveOut {
    SolveKind kind = SK_ERROR;
    double value = 0;
};

static bool evalWithX(const String &side, double x, double &out) {
    EvalCtx ctx;
    ctx.xBound = true;
    ctx.xValue = x;
    return evaluateExpr(side, ctx, out);
}

static SolveOut solveExpression(const String &expr) {
    SolveOut so;
    int eq = expr.indexOf('=');
    if (eq < 0) {
        double r;
        if (!evaluateNumeric(expr, r)) {
            so.kind = SK_ERROR;
            return so;
        }
        so.kind = SK_NUMERIC;
        so.value = r;
        return so;
    }
    // Exactly one '='
    if (expr.indexOf('=', eq + 1) >= 0) {
        so.kind = SK_ERROR;
        return so;
    }
    String lhs = expr.substring(0, eq);
    String rhs = expr.substring(eq + 1);
    lhs.trim();
    rhs.trim();
    if (lhs.length() == 0 || rhs.length() == 0) {
        so.kind = SK_ERROR;
        return so;
    }

    bool hasX = sideHasX(lhs) || sideHasX(rhs);
    if (!hasX) {
        double l, r;
        if (!evaluateNumeric(lhs, l) || !evaluateNumeric(rhs, r)) {
            so.kind = SK_ERROR;
            return so;
        }
        so.kind = (fabs(l - r) < 1e-9) ? SK_TRUE : SK_FALSE;
        return so;
    }

    auto f = [&](double v, double &out) -> bool {
        double a, b;
        if (!evalWithX(lhs, v, a) || !evalWithX(rhs, v, b)) return false;
        out = a - b;
        return true;
    };

    double f0, f1, f2, fm1;
    if (!f(0, f0) || !f(1, f1) || !f(2, f2) || !f(-1, fm1)) {
        so.kind = SK_ERROR;
        return so;
    }
    double b = f0;
    double a = f1 - b;

    // Linearity check
    double pred2 = 2 * a + b;
    double predm1 = -a + b;
    double scale = fabs(a) + fabs(b) + 1.0;
    if (fabs(f2 - pred2) > 1e-6 * scale || fabs(fm1 - predm1) > 1e-6 * scale) {
        so.kind = SK_LINEAR_ONLY;
        return so;
    }
    if (fabs(a) < 1e-12) {
        so.kind = (fabs(b) < 1e-9) ? SK_ANY : SK_NONE;
        return so;
    }
    so.kind = SK_X;
    so.value = -b / a;
    return so;
}

// ---- History ---------------------------------------------------------------

struct Hist {
    String items[kHistCap];
    int count = 0;
    int cursor = -1; // -1 = live expr

    void push(const String &e) {
        if (e.length() == 0) return;
        if (count > 0 && items[0] == e) return;
        for (int i = kHistCap - 1; i > 0; i--) items[i] = items[i - 1];
        items[0] = e;
        if (count < kHistCap) count++;
        cursor = -1;
    }

    bool move(int dir, String &expr, int &caret) {
        if (count == 0) return false;
        if (cursor < 0) cursor = 0;
        else {
            cursor += dir;
            if (cursor < 0) cursor = 0;
            if (cursor >= count) cursor = count - 1;
        }
        expr = items[cursor];
        caret = (int)expr.length();
        return true;
    }
};

// ---- UI --------------------------------------------------------------------

static void drawCalcChrome() {
    tft.fillScreen(kvxConfig.bgColor);
    drawKvxTopBar("Calculator");
}

static void drawHelpOverlay() {
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("Calc Help");
        const uint16_t bg = kvxConfig.bgColor;
        const uint16_t pri = kvxConfig.priColor;
        tft.setTextSize(FP);
        tft.setTextColor(pri, bg);
        int y = KVX_TOPBAR_H + 4;
        auto line = [&](const char *s) {
            tft.setCursor(4, y);
            tft.print(s);
            y += FP * LH + 1;
        };
        line("Enter=eval  Del=bksp  `=clr");
        line("Fn=funcs  Fn+arrows=caret");
        line("Fn+up/dn=history  Opt=help");
        line("*=mul  x=unknown  /=div");
        line("ex: sin(30)  2^10  2x=4");
        tft.setTextColor(kvxConfig.secColor, bg);
        tft.drawCentreString("any key = back", tftWidth / 2, tftHeight - FP * LH - 2, 1);
    }

    for (;;) {
        if (check(EscPress) || forceHome) break;
        if (check(AnyKeyPress) || check(SelPress) || check(UpPress) || check(DownPress) ||
            check(PrevPress) || check(NextPress))
            break;
        keyStroke k = _getKeyPress();
        if (k.pressed) break;
        delay(20);
    }
    consumeNavFlags();
}

static void drawCalcBodyInner(
    const String &expr, int caret, const String &status, uint16_t statusColor, bool blinkOn
) {
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t sec = kvxConfig.secColor;
    const int bodyY = KVX_TOPBAR_H + 4;
    const int bodyH = tftHeight - bodyY - 14;

    tft.fillRect(0, bodyY, tftWidth, bodyH + 14, bg);

    // Expression with caret (right-aligned, keep caret visible).
    String shown = expr;
    int caretShown = caret;
    if (shown.length() == 0) {
        shown = " ";
        caretShown = 0;
    }
    int maxChars = max(1, (tftWidth - 16) / (FP * LW));
    if ((int)shown.length() > maxChars) {
        // Prefer keeping caret near the right end.
        int start = caretShown - maxChars + 1;
        if (start < 0) start = 0;
        if (start + maxChars > (int)shown.length()) start = (int)shown.length() - maxChars;
        shown = shown.substring(start, start + maxChars);
        caretShown = caretShown - start;
    }

    tft.setTextSize(FP);
    int xRight = tftWidth - 8;
    int yExpr = bodyY + 4;
    // Draw left of caret, caret glyph, right of caret — right-aligned as a block.
    String left = shown.substring(0, caretShown);
    String right = shown.substring(caretShown);
    char caretCh = blinkOn ? '|' : ' ';
    String block = left + caretCh + right;
    tft.setTextColor(pri, bg);
    tft.drawRightString(block, xRight, yExpr, 1);

    // DEG/RAD chip (left) + pending-op chip (right).
    const int chipY = bodyY + FP * LH + 8;
    const int chipH = FM * LH + 4;
    {
        const char *mode = gDegMode ? "DEG" : "RAD";
        const int chipW = 3 * FM * LW + 6;
        tft.fillRoundRect(8, chipY, chipW, chipH, 3, sec);
        tft.setTextSize(FM);
        tft.setTextColor(bg, sec);
        tft.drawCentreString(mode, 8 + chipW / 2, chipY + 2, 1);
    }
    char pending = 0;
    if (expr.length() && caret == (int)expr.length() && isBinaryOp(expr[expr.length() - 1]))
        pending = expr[expr.length() - 1];
    if (pending) {
        char chip[2] = {pending, 0};
        const int chipW = FM * LW + 6;
        const int chipX = tftWidth - 8 - chipW;
        tft.fillRoundRect(chipX, chipY, chipW, chipH, 3, sec);
        tft.setTextSize(FM);
        tft.setTextColor(bg, sec);
        tft.drawCentreString(chip, chipX + chipW / 2, chipY + 2, 1);
    }

    const int statusY = bodyY + FP * LH + FM * LH + 18;
    tft.setTextSize(FG);
    tft.setTextColor(statusColor, bg);
    String st = status.length() ? status : " ";
    int stMax = max(1, (tftWidth - 16) / (FG * LW));
    if ((int)st.length() > stMax) st = st.substring(st.length() - stMax);
    tft.drawRightString(st, tftWidth - 8, statusY, 1);

    tft.setTextSize(FP);
    tft.setTextColor(sec, bg);
#ifdef HAS_KEYBOARD
    tft.drawCentreString("Enter=eval Fn=fn `=clr ESC", tftWidth / 2, tftHeight - FP * LH - 2, 1);
#else
    tft.drawCentreString("OK=edit  Dn=fn  hold=back", tftWidth / 2, tftHeight - FP * LH - 2, 1);
#endif
}

static void drawCalcBody(
    const String &expr, int caret, const String &status, uint16_t statusColor, bool blinkOn
) {
    TftFrame frame;
    drawCalcBodyInner(expr, caret, status, statusColor, blinkOn);
}

static void drawCalcScreen(
    const String &expr, int caret, const String &status, uint16_t statusColor, bool blinkOn
) {
    TftFrame frame;
    drawCalcChrome();
    drawCalcBodyInner(expr, caret, status, statusColor, blinkOn);
}

struct PickerResult {
    bool toggledDeg = false;
    String insert;
    int caretBack = 0; // move caret left this many after insert (before ')')
};

static PickerResult runFunctionPicker() {
    PickerResult pr;
    String chosen;
    int caretBack = 0;
    bool toggleDeg = false;

    std::vector<Option> options;
    auto addIns = [&](const char *label, const char *ins, int back) {
        options.push_back({label, [&chosen, &caretBack, ins, back]() {
                               chosen = ins;
                               caretBack = back;
                           }});
    };
    options.push_back({gDegMode ? "Mode: DEG -> RAD" : "Mode: RAD -> DEG", [&toggleDeg]() {
                           toggleDeg = true;
                       }});
    addIns("sin(", "sin()", 1);
    addIns("cos(", "cos()", 1);
    addIns("tan(", "tan()", 1);
    addIns("asin(", "asin()", 1);
    addIns("acos(", "acos()", 1);
    addIns("atan(", "atan()", 1);
    addIns("ln(", "ln()", 1);
    addIns("log(", "log()", 1);
    addIns("log2(", "log2()", 1);
    addIns("exp(", "exp()", 1);
    addIns("abs(", "abs()", 1);
    addIns("sqrt(", "sqrt()", 1);
    addIns("fact(", "fact()", 1);
    addIns("pi", "pi", 0);
    addIns("e", "e", 0);
    addIns("ans", "ans", 0);
    addIns("^", "^", 0);
    addIns("%", "%", 0);

    loopOptions(options, "Functions");
    consumeNavFlags();

    if (toggleDeg) {
        gDegMode = !gDegMode;
        pr.toggledDeg = true;
        return pr;
    }
    pr.insert = chosen;
    pr.caretBack = caretBack;
    return pr;
}

static void applySolveToUi(
    const SolveOut &so, String &expr, int &caret, String &status, uint16_t &statusColor,
    bool &showedResult, Hist &hist
) {
    switch (so.kind) {
        case SK_NUMERIC:
            hist.push(expr);
            status = formatStatusNumeric(so.value);
            statusColor = kvxConfig.secColor;
            expr = formatResult(so.value);
            caret = (int)expr.length();
            gAns = so.value;
            gHasAns = true;
            showedResult = true;
            break;
        case SK_TRUE:
            hist.push(expr);
            status = "True";
            statusColor = kvxConfig.secColor;
            showedResult = false;
            break;
        case SK_FALSE:
            hist.push(expr);
            status = "False";
            statusColor = kvxConfig.secColor;
            showedResult = false;
            break;
        case SK_X:
            hist.push(expr);
            status = "x = " + formatResult(so.value);
            statusColor = kvxConfig.secColor;
            gAns = so.value;
            gHasAns = true;
            showedResult = false;
            break;
        case SK_ANY:
            hist.push(expr);
            status = "any x";
            statusColor = kvxConfig.secColor;
            showedResult = false;
            break;
        case SK_NONE:
            hist.push(expr);
            status = "no solution";
            statusColor = TFT_RED;
            showedResult = false;
            break;
        case SK_LINEAR_ONLY:
            status = "linear only";
            statusColor = TFT_RED;
            showedResult = false;
            break;
        case SK_ERROR:
        default:
            status = "Error";
            statusColor = TFT_RED;
            showedResult = false;
            break;
    }
}

} // namespace

void calculatorApp() {
    String expr = "";
    int caret = 0;
    String status = "";
    uint16_t statusColor = kvxConfig.secColor;
    bool showedResult = false;
    Hist hist;
    bool blinkOn = true;
    unsigned long lastBlink = millis();

    drawCalcScreen(expr, caret, status, statusColor, blinkOn);

    auto redraw = [&]() { drawCalcBody(expr, caret, status, statusColor, blinkOn); };
    auto redrawFull = [&]() { drawCalcScreen(expr, caret, status, statusColor, blinkOn); };

    auto doEval = [&]() {
        SolveOut so = solveExpression(expr);
        applySolveToUi(so, expr, caret, status, statusColor, showedResult, hist);
    };

    auto insertText = [&](const String &ins, int caretBack) {
        if (showedResult) {
            // Digits / letters / '(' start fresh; operators chain — handled by callers.
        }
        insertAtCaret(expr, caret, ins);
        if (caretBack > 0) {
            caret -= caretBack;
            if (caret < 0) caret = 0;
        }
        status = "";
    };

    for (;;) {
        if (check(EscPress) || forceHome) break;

#ifndef LITE_VERSION
        if (pdaAlarmsPoll()) {
            redrawFull();
        }
#endif

        unsigned long now = millis();
        if (now - lastBlink >= kBlinkMs) {
            lastBlink = now;
            blinkOn = !blinkOn;
            redraw();
        }

#ifdef HAS_KEYBOARD
        keyStroke key = _getKeyPress();
        if (!key.pressed) {
            delay(20);
            continue;
        }

        bool changed = false;

        // Opt (gui) → help
        if (key.gui) {
            drawHelpOverlay();
            redrawFull();
            changed = false;
        } else if (key.fn) {
            bool hidArrow = false;
            bool helpH = false;
            for (char raw : key.word) {
                unsigned char c = (unsigned char)raw;
                if (c == 0xD8) {
                    if (caret > 0) caret--;
                    hidArrow = true;
                    changed = true;
                } else if (c == 0xD7) {
                    if (caret < (int)expr.length()) caret++;
                    hidArrow = true;
                    changed = true;
                } else if (c == 0xDA) {
                    // Fn+up → older history
                    hist.move(1, expr, caret);
                    showedResult = false;
                    status = "";
                    hidArrow = true;
                    changed = true;
                } else if (c == 0xD9) {
                    // Fn+down → newer history
                    hist.move(-1, expr, caret);
                    showedResult = false;
                    status = "";
                    hidArrow = true;
                    changed = true;
                } else if (c == 'h' || c == 'H') {
                    helpH = true;
                }
            }
            if (helpH) {
                drawHelpOverlay();
                redrawFull();
                changed = false;
            } else if (!hidArrow) {
                // Fn alone → function picker
                PickerResult pr = runFunctionPicker();
                if (pr.toggledDeg) {
                    changed = true;
                } else if (pr.insert.length()) {
                    if (showedResult) {
                        char c0 = pr.insert[0];
                        if (!isBinaryOp(c0)) {
                            expr = "";
                            caret = 0;
                        }
                        showedResult = false;
                        status = "";
                    }
                    insertText(pr.insert, pr.caretBack);
                    changed = true;
                } else {
                    changed = true; // redraw after picker
                }
                redrawFull();
                changed = false;
            }
        } else if (key.enter) {
            check(SelPress);
            doEval();
            changed = true;
        } else if (key.del) {
            backspaceAt(expr, caret);
            status = "";
            showedResult = false;
            changed = true;
        } else {
            for (char raw : key.word) {
                unsigned char uc = (unsigned char)raw;
                if (isHidCmd(uc)) continue;
                char c = (char)uc;

                if (c == '`') {
                    expr = "";
                    caret = 0;
                    status = "";
                    showedResult = false;
                    changed = true;
                    continue;
                }

                bool isLetter = isalpha((unsigned char)c);
                bool valid = isdigit((unsigned char)c) || c == '.' || isBinaryOp(c) || c == '(' ||
                             c == ')' || c == '=' || isLetter;
                // Allow lowercase/uppercase letters for names and x.
                if (!valid) continue;

                if (showedResult) {
                    if (isdigit((unsigned char)c) || c == '.' || c == '(' || isLetter) {
                        expr = "";
                        caret = 0;
                    }
                    showedResult = false;
                    status = "";
                }

                if (isBinaryOp(c)) {
                    insertOrReplaceOp(expr, caret, c);
                } else {
                    // Normalize X → x for the unknown.
                    if (c == 'X') c = 'x';
                    char buf[2] = {c, 0};
                    insertAtCaret(expr, caret, String(buf));
                }
                changed = true;
            }
        }

        if (changed) redraw();

#else
        // StickS3 / no keyboard: Sel = edit via keyboard(), Down = function picker.
        if (check(SelPress)) {
            String prev = expr;
            String edited = keyboard(expr, kMaxExpr, "Expression");
            if (edited != String((char)0x1B)) {
                expr = edited;
                if ((int)expr.length() > kMaxExpr) expr.remove(kMaxExpr);
                caret = (int)expr.length();
                doEval();
            } else {
                expr = prev;
                caret = (int)expr.length();
            }
            redrawFull();
        } else if (check(DownPress)) {
            PickerResult pr = runFunctionPicker();
            if (pr.toggledDeg) {
                // mode chip updates
            } else if (pr.insert.length()) {
                if (showedResult && !isBinaryOp(pr.insert[0])) {
                    expr = "";
                    caret = 0;
                    showedResult = false;
                }
                insertAtCaret(expr, caret, pr.insert);
                if (pr.caretBack > 0) {
                    caret -= pr.caretBack;
                    if (caret < 0) caret = 0;
                }
                status = "";
            }
            redrawFull();
        } else {
            delay(20);
        }
#endif
    }

    consumeNavFlags();
    tft.fillScreen(kvxConfig.bgColor);
}
