#include "game/content/Parser.h"

namespace ktd::content {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isKeyChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c) || c == '_' || c == '-';
}

// Characters that may appear inside a number token. Scanned greedily so that
// `1e-3` stays one token; a malformed run like `1-2` is also one token, and is
// rejected by the exact converters below rather than being silently split.
bool isNumberChar(char c) {
    return isDigit(c) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E';
}

// Powers of ten that are exactly representable as a double. 10^22 is the last
// one; beyond it the literal is already rounded and the "one correctly rounded
// operation" property that ADR 0005 buys is lost.
constexpr int kMaxExponent = 22;
constexpr double kPow10[kMaxExponent + 1] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

// 10^15 < 2^53, so a mantissa of at most 15 significant digits converts to
// double exactly. Sixteen would not, and the exactness is the whole point.
constexpr int kMaxSignificantDigits = 15;

class Parser {
public:
    Parser(std::string_view text, std::string_view file, Document& out, std::vector<Error>& errors)
        : text_(text),
          file_(file),
          out_(out),
          errors_(errors) {}

    bool run() {
        const std::size_t before = errors_.size();
        out_.file = std::string(file_);

        while (!atEnd()) {
            skipSpace();
            if (atEnd()) {
                break;
            }
            const char c = peek();
            if (c == '\n') {
                advance();
            } else if (c == '#') {
                skipLine();
            } else if (c == '[') {
                parseHeader();
            } else if (isKeyChar(c)) {
                parseKeyValue();
            } else {
                error("expected a table header, a key, or a comment");
                skipLine();
            }
        }
        return errors_.size() == before;
    }

private:
    // --- character-level helpers -------------------------------------------

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
    char peekAt(std::size_t offset) const {
        return pos_ + offset < text_.size() ? text_[pos_ + offset] : '\0';
    }

    void advance() {
        if (atEnd()) {
            return;
        }
        if (text_[pos_] == '\n') {
            ++line_;
            lineStart_ = pos_ + 1;
        }
        ++pos_;
    }

    int column() const { return static_cast<int>(pos_ - lineStart_) + 1; }

    void skipSpace() {
        while (!atEnd() && isSpace(peek())) {
            advance();
        }
    }

    // Skips to the start of the next line. This is the resynchronisation point
    // after an error: the format is line-oriented, so one bad line costs
    // exactly one message instead of cascading.
    void skipLine() {
        while (!atEnd() && peek() != '\n') {
            advance();
        }
        if (!atEnd()) {
            advance();
        }
    }

    void error(std::string message) { error(line_, column(), std::move(message)); }

    void error(int line, int col, std::string message) {
        errors_.push_back(Error{std::string(file_), line, col, std::move(message)});
    }

    // --- token-level helpers ------------------------------------------------

    std::string_view takeKey() {
        const std::size_t start = pos_;
        while (!atEnd() && isKeyChar(peek())) {
            advance();
        }
        return text_.substr(start, pos_ - start);
    }

    // Everything after a complete construct must be blank or a comment. Without
    // this check `damage = 10 20` would silently keep the 10.
    bool expectLineEnd(const char* what) {
        skipSpace();
        if (atEnd() || peek() == '\n') {
            return true;
        }
        if (peek() == '#') {
            skipLine();
            return true;
        }
        error(std::string("unexpected text after ") + what);
        skipLine();
        return false;
    }

    // --- constructs ---------------------------------------------------------

    void parseHeader() {
        const int headerLine = line_;
        const int headerColumn = column();

        advance();  // '['
        bool arrayElement = false;
        if (peek() == '[') {
            arrayElement = true;
            advance();
        }

        std::vector<std::string_view> path;
        for (;;) {
            skipSpace();
            if (!isKeyChar(peek())) {
                error("expected a name in the table header");
                skipLine();
                return;
            }
            path.push_back(takeKey());
            skipSpace();
            if (peek() == '.') {
                advance();
                continue;
            }
            break;
        }

        if (peek() != ']') {
            error("expected ']' to close the table header");
            skipLine();
            return;
        }
        advance();
        if (arrayElement) {
            if (peek() != ']') {
                error("expected ']]' to close an array-of-tables header");
                skipLine();
                return;
            }
            advance();
        }
        expectLineEnd("a table header");

        // A repeated [header] is nearly always a copy-paste that silently
        // discards one of the two blocks, so it is an error. [[header]] is the
        // spelling that means "there are several of these".
        if (!arrayElement) {
            for (const Table& existing : out_.tables) {
                if (!existing.arrayElement && existing.path == path) {
                    error(headerLine, headerColumn,
                          "duplicate table '" + joined(path) + "', first declared on line " +
                              std::to_string(existing.line) +
                              " (use [[...]] if several were intended)");
                    break;
                }
            }
        }

        Table table;
        table.path = std::move(path);
        table.arrayElement = arrayElement;
        table.line = headerLine;
        out_.tables.push_back(std::move(table));
    }

    void parseKeyValue() {
        const int keyLine = line_;
        const int keyColumn = column();
        const std::string_view key = takeKey();

        skipSpace();
        if (peek() != '=') {
            error("expected '=' after key '" + std::string(key) + "'");
            skipLine();
            return;
        }
        advance();
        skipSpace();

        Value value;
        if (!parseValue(value)) {
            skipLine();
            return;
        }
        expectLineEnd("a value");

        if (out_.tables.empty()) {
            error(keyLine, keyColumn,
                  "key '" + std::string(key) + "' appears before any [table] header");
            return;
        }

        Table& table = out_.tables.back();
        for (const KeyValue& existing : table.entries) {
            if (existing.key == key) {
                error(keyLine, keyColumn,
                      "duplicate key '" + std::string(key) + "', first set on line " +
                          std::to_string(existing.line));
                return;
            }
        }
        table.entries.push_back(KeyValue{key, std::move(value), keyLine, keyColumn});
    }

    bool parseValue(Value& out) {
        out.line = line_;
        out.column = column();

        const char c = peek();
        if (c == '"') {
            return parseString(out);
        }
        if (c == '[') {
            return parseArray(out);
        }
        if (matchWord("true")) {
            out.kind = ValueKind::Bool;
            out.integer = 1;
            return true;
        }
        if (matchWord("false")) {
            out.kind = ValueKind::Bool;
            out.integer = 0;
            return true;
        }
        if (isNumberChar(c)) {
            return parseNumber(out);
        }
        error("expected a value");
        return false;
    }

    bool matchWord(std::string_view word) {
        if (text_.compare(pos_, word.size(), word) != 0) {
            return false;
        }
        // Guard against `truest` matching `true`.
        if (isKeyChar(peekAt(word.size()))) {
            return false;
        }
        for (std::size_t i = 0; i < word.size(); ++i) {
            advance();
        }
        return true;
    }

    bool parseString(Value& out) {
        advance();  // opening quote
        const std::size_t start = pos_;
        while (!atEnd() && peek() != '"' && peek() != '\n') {
            advance();
        }
        if (atEnd() || peek() == '\n') {
            error("unterminated string");
            return false;
        }
        out.kind = ValueKind::String;
        out.text = text_.substr(start, pos_ - start);
        advance();  // closing quote
        return true;
    }

    bool parseNumber(Value& out) {
        const int numberLine = line_;
        const int numberColumn = column();
        const std::size_t start = pos_;
        while (!atEnd() && isNumberChar(peek())) {
            advance();
        }
        const std::string_view token = text_.substr(start, pos_ - start);

        const bool looksReal = token.find('.') != std::string_view::npos ||
                               token.find('e') != std::string_view::npos ||
                               token.find('E') != std::string_view::npos;

        if (looksReal) {
            float number = 0.0f;
            if (!parseFloatExact(token, number)) {
                error(numberLine, numberColumn,
                      "'" + std::string(token) +
                          "' is not a number this format converts exactly (at most " +
                          std::to_string(kMaxSignificantDigits) +
                          " significant digits and an exponent within +-" +
                          std::to_string(kMaxExponent) + ")");
                return false;
            }
            out.kind = ValueKind::Float;
            out.number = number;
            return true;
        }

        std::int64_t integer = 0;
        if (!parseIntExact(token, integer)) {
            error(numberLine, numberColumn, "'" + std::string(token) + "' is not a whole number");
            return false;
        }
        out.kind = ValueKind::Integer;
        out.integer = integer;
        return true;
    }

    // Skips whitespace, newlines and comments. Only legal inside brackets, so a
    // long array can be laid out over several lines.
    void skipInsideArray() {
        for (;;) {
            skipSpace();
            if (peek() == '\n') {
                advance();
                continue;
            }
            if (peek() == '#') {
                skipLine();
                continue;
            }
            return;
        }
    }

    bool parseArray(Value& out) {
        const int arrayLine = line_;
        const int arrayColumn = column();
        out.kind = ValueKind::Array;
        advance();  // '['

        for (;;) {
            skipInsideArray();
            if (atEnd()) {
                error(arrayLine, arrayColumn, "unterminated array");
                return false;
            }
            if (peek() == ']') {
                advance();
                return true;
            }

            Value element;
            if (!parseValue(element)) {
                return false;
            }
            if (element.kind == ValueKind::Array) {
                error(element.line, element.column, "arrays may not be nested");
                return false;
            }

            // Homogeneity is checked here rather than by every reader. A mixed
            // array is always a mistake, and catching it at the element makes
            // the message point at the odd one out.
            if (!out.elementKindKnown) {
                out.elementKind = element.kind;
                out.elementKindKnown = true;
            } else if (element.kind != out.elementKind) {
                // Integers are allowed to appear in a float array, and promote
                // it: [1, 2.5] is how anyone would write it.
                const bool bothNumeric =
                    element.isNumber() &&
                    (out.elementKind == ValueKind::Integer || out.elementKind == ValueKind::Float);
                if (!bothNumeric) {
                    error(element.line, element.column,
                          std::string("array mixes ") + nameOf(out.elementKind) + " and " +
                              nameOf(element.kind) + " values");
                    return false;
                }
                if (element.kind == ValueKind::Float) {
                    out.elementKind = ValueKind::Float;
                }
            }
            out.elements.push_back(std::move(element));

            skipInsideArray();
            if (peek() == ',') {
                advance();
                continue;
            }
            if (peek() == ']') {
                advance();
                return true;
            }
            error("expected ',' or ']' in array");
            return false;
        }
    }

    static std::string joined(const std::vector<std::string_view>& path) {
        std::string text;
        for (const std::string_view part : path) {
            if (!text.empty()) {
                text += '.';
            }
            text.append(part);
        }
        return text;
    }

    std::string_view text_;
    std::string_view file_;
    Document& out_;
    std::vector<Error>& errors_;
    std::size_t pos_ = 0;
    std::size_t lineStart_ = 0;
    int line_ = 1;
};

}  // namespace

const char* nameOf(ValueKind kind) {
    switch (kind) {
        case ValueKind::String:
            return "string";
        case ValueKind::Integer:
            return "integer";
        case ValueKind::Float:
            return "float";
        case ValueKind::Bool:
            return "boolean";
        case ValueKind::Array:
            return "array";
    }
    return "value";
}

std::string Error::format() const {
    return file + ":" + std::to_string(line) + ":" + std::to_string(column) + ": " + message;
}

const Value* Table::find(std::string_view key) const {
    for (const KeyValue& entry : entries) {
        if (entry.key == key) {
            return &entry.value;
        }
    }
    return nullptr;
}

bool parse(std::string_view text, std::string_view file, Document& out,
           std::vector<Error>& errors) {
    Parser parser(text, file, out, errors);
    return parser.run();
}

bool parseIntExact(std::string_view text, std::int64_t& out) {
    std::size_t i = 0;
    bool negative = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
        negative = text[i] == '-';
        ++i;
    }
    if (i >= text.size()) {
        return false;
    }

    std::uint64_t magnitude = 0;
    for (; i < text.size(); ++i) {
        if (!isDigit(text[i])) {
            return false;
        }
        // The limit is checked before the multiply so the overflow never
        // happens rather than being detected after the fact.
        if (magnitude > 922337203685477580ull) {
            return false;
        }
        magnitude = magnitude * 10 + static_cast<std::uint64_t>(text[i] - '0');
    }

    const std::uint64_t limit = negative ? 9223372036854775808ull : 9223372036854775807ull;
    if (magnitude > limit) {
        return false;
    }
    out = negative ? -static_cast<std::int64_t>(magnitude) : static_cast<std::int64_t>(magnitude);
    return true;
}

bool parseFloatExact(std::string_view text, float& out) {
    std::size_t i = 0;
    bool negative = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
        negative = text[i] == '-';
        ++i;
    }

    std::uint64_t mantissa = 0;
    int significantDigits = 0;
    int fractionDigits = 0;
    bool sawDigit = false;

    const auto consumeDigit = [&](char c) {
        sawDigit = true;
        const int digit = c - '0';
        // Leading zeros carry no information, so they do not count against the
        // significant-digit budget.
        if (mantissa == 0 && digit == 0) {
            return true;
        }
        if (significantDigits >= kMaxSignificantDigits) {
            return false;
        }
        mantissa = mantissa * 10 + static_cast<std::uint64_t>(digit);
        ++significantDigits;
        return true;
    };

    while (i < text.size() && isDigit(text[i])) {
        if (!consumeDigit(text[i])) {
            return false;
        }
        ++i;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && isDigit(text[i])) {
            if (!consumeDigit(text[i])) {
                return false;
            }
            ++fractionDigits;
            ++i;
        }
    }
    if (!sawDigit) {
        return false;
    }

    int exponent = -fractionDigits;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        bool exponentNegative = false;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            exponentNegative = text[i] == '-';
            ++i;
        }
        if (i >= text.size() || !isDigit(text[i])) {
            return false;
        }
        int magnitude = 0;
        for (; i < text.size() && isDigit(text[i]); ++i) {
            if (magnitude > 1000) {
                return false;
            }
            magnitude = magnitude * 10 + (text[i] - '0');
        }
        exponent += exponentNegative ? -magnitude : magnitude;
    }

    if (i != text.size()) {
        return false;  // trailing junk, or a second sign, or a second '.'
    }
    if (exponent > kMaxExponent || exponent < -kMaxExponent) {
        return false;
    }

    // The one arithmetic operation. Both operands are exact doubles -- the
    // mantissa because it is under 2^53, the power of ten because it is in the
    // table above -- so IEEE 754 requires the result to be the correctly
    // rounded double, identically on every platform. Narrowing that to float is
    // likewise a single correctly rounded conversion.
    double value = static_cast<double>(mantissa);
    if (exponent >= 0) {
        value *= kPow10[exponent];
    } else {
        value /= kPow10[-exponent];
    }
    if (negative) {
        value = -value;
    }
    out = static_cast<float>(value);
    return true;
}

}  // namespace ktd::content
