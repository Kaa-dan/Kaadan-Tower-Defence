#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A documented TOML subset with a hand-written parser. See ADR 0005 for why
// this exists instead of a JSON or TOML library; the short version is that the
// number parsing has to be defined exactly rather than inherited from whichever
// standard library the target platform ships.
//
// The subset, in full:
//
//     # comments run to end of line
//     [table.path]          a table; the path may not repeat
//     [[table.path]]        an element of an array of tables; may repeat
//     key = "text"          strings are raw bytes between quotes, no escapes
//     key = 42              integers
//     key = -1.5e3          floats
//     key = true            booleans
//     key = [1, 2, 3]       flat, homogeneous arrays of the above
//
// Anything else is an error rather than a guess. There are no dotted keys, no
// inline tables, no datetimes, no multi-line strings and no escape sequences --
// every one of those is a feature this game does not need and a piece of
// parser that could be subtly wrong.
//
// Parsing operates on a std::string_view. Parsing a string is not file I/O, so
// this lives in game/ without breaking purity, and every test in this phase
// runs from a string literal without touching a disk.

namespace ktd::content {

// A problem, located precisely enough to fix without searching.
//
// Errors are collected rather than thrown at the first one. A balance file
// picks up several typos per editing session, and a parser that reports one
// message per rebuild turns a two-minute fix into ten.
struct Error {
    std::string file;
    int line = 0;    // 1-based
    int column = 0;  // 1-based, counted in bytes
    std::string message;

    // "content/towers.ktd:12:8: expected a value"
    std::string format() const;
};

enum class ValueKind : std::uint8_t {
    String,
    Integer,
    Float,
    Bool,
    Array,
};

const char* nameOf(ValueKind kind);

// One authored value. Small enough to copy, and `text` is a view into the
// source, so the source text must outlive the Document.
struct Value {
    ValueKind kind = ValueKind::String;

    std::string_view text{};   // String: the bytes between the quotes
    std::int64_t integer = 0;  // Integer; also Bool, as 0 or 1
    float number = 0.0f;       // Float

    // Arrays are flat and homogeneous. An empty array has no element kind, so
    // it satisfies any request.
    ValueKind elementKind = ValueKind::String;
    bool elementKindKnown = false;
    std::vector<Value> elements;

    int line = 0;
    int column = 0;

    bool isNumber() const { return kind == ValueKind::Integer || kind == ValueKind::Float; }

    // Integers are accepted where a float is wanted, so `range = 3` does not
    // have to be written `3.0`. The conversion is exact for every integer the
    // subset admits into a balance file.
    float asFloat() const {
        return kind == ValueKind::Integer ? static_cast<float>(integer) : number;
    }
};

struct KeyValue {
    std::string_view key;
    Value value;
    int line = 0;
    int column = 0;
};

// One [header] and the keys under it, in authored order.
struct Table {
    std::vector<std::string_view> path;  // [tower.arrow] -> {"tower", "arrow"}
    std::vector<KeyValue> entries;
    bool arrayElement = false;  // declared with [[...]] rather than [...]
    int line = 0;

    const Value* find(std::string_view key) const;

    // The head of the path is the kind of thing being declared and the tail is
    // usually its id: [tower.arrow] declares a tower called "arrow".
    bool isKind(std::string_view head) const { return !path.empty() && path.front() == head; }
    std::string_view leaf() const { return path.empty() ? std::string_view{} : path.back(); }
};

struct Document {
    std::string file;
    std::vector<Table> tables;
};

// Parses `text` in full, appending every problem to `errors`. Returns true when
// none were found.
//
// On a bad line the parser records the error and resynchronises at the next
// newline, so one stray character does not cascade into a hundred messages.
bool parse(std::string_view text, std::string_view file, Document& out, std::vector<Error>& errors);

// The deterministic decimal-to-float conversion, exposed because it is the part
// of this file the whole hand-rolled-parser argument rests on, and so is tested
// directly against expected bit patterns.
//
// The algorithm is fixed by ADR 0005 and is not an implementation detail:
// accumulate every significant digit into a uint64 mantissa, then perform one
// IEEE double multiply or divide by an exactly-representable power of ten, then
// narrow to float. A single division of two exactly-representable doubles is
// correctly rounded on every conforming platform, which strtof and from_chars
// are only guaranteed to be in principle.
//
// Returns false -- rather than rounding, saturating or guessing -- for anything
// the exact path cannot represent: more than 15 significant digits, or a
// decimal exponent outside +-22 (10^22 is the largest power of ten that is
// exact in a double).
bool parseFloatExact(std::string_view text, float& out);

// The same conversion's integer half, for keys that must be whole numbers.
bool parseIntExact(std::string_view text, std::int64_t& out);

}  // namespace ktd::content
