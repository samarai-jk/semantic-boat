#include "semantic_display/compiler.hpp"
#include <cstring>
#include <limits>

namespace semantic_display {
namespace {

constexpr std::size_t recordHeaderSize = 4u;

void write16(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8u);
}

void write32(std::uint8_t* destination, std::uint32_t value) {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8u);
    destination[2] = static_cast<std::uint8_t>(value >> 16u);
    destination[3] = static_cast<std::uint8_t>(value >> 24u);
}

std::uint16_t read16(const std::uint8_t* source) {
    return static_cast<std::uint16_t>(source[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(source[1]) << 8u);
}

struct Token {
    char bytes[256]{};
    std::uint8_t size{};

    std::string_view view() const { return {bytes, size}; }
    bool equals(std::string_view other) const { return view() == other; }
};

class JsonReader {
public:
    JsonReader(std::string_view input, std::uint8_t maxStringBytes, std::uint8_t maxDepth)
        : input_(input), maxStringBytes_(maxStringBytes), maxDepth_(maxDepth) {}

    std::size_t position() const { return position_; }
    bool atEnd() { skipWhitespace(); return position_ == input_.size(); }

    bool consume(char expected) {
        skipWhitespace();
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    bool peek(char expected) {
        skipWhitespace();
        return position_ < input_.size() && input_[position_] == expected;
    }

    bool string(Token& result) {
        result.size = 0u;
        if (!consume('"')) return false;
        while (position_ < input_.size()) {
            auto value = static_cast<unsigned char>(input_[position_++]);
            if (value == '"') return true;
            if (value < 0x20u) return false;
            if (value == '\\') {
                if (position_ >= input_.size()) return false;
                const auto escaped = input_[position_++];
                switch (escaped) {
                case '"': value = '"'; break;
                case '\\': value = '\\'; break;
                case '/': value = '/'; break;
                case 'b': value = '\b'; break;
                case 'f': value = '\f'; break;
                case 'n': value = '\n'; break;
                case 'r': value = '\r'; break;
                case 't': value = '\t'; break;
                case 'u': {
                    std::uint16_t code{};
                    for (std::uint8_t digit = 0u; digit < 4u; ++digit) {
                        if (position_ >= input_.size()) return false;
                        const auto hex = input_[position_++];
                        code = static_cast<std::uint16_t>(code << 4u);
                        if (hex >= '0' && hex <= '9') code |= static_cast<std::uint16_t>(hex - '0');
                        else if (hex >= 'a' && hex <= 'f') code |= static_cast<std::uint16_t>(hex - 'a' + 10);
                        else if (hex >= 'A' && hex <= 'F') code |= static_cast<std::uint16_t>(hex - 'A' + 10);
                        else return false;
                    }
                    if (code >= 0xd800u && code <= 0xdfffu) return false;
                    if (code <= 0x7fu) {
                        if (!append(result, static_cast<std::uint8_t>(code))) return false;
                    } else if (code <= 0x7ffu) {
                        if (!append(result, static_cast<std::uint8_t>(0xc0u | (code >> 6u))) ||
                            !append(result, static_cast<std::uint8_t>(0x80u | (code & 0x3fu)))) return false;
                    } else {
                        if (!append(result, static_cast<std::uint8_t>(0xe0u | (code >> 12u))) ||
                            !append(result, static_cast<std::uint8_t>(0x80u | ((code >> 6u) & 0x3fu))) ||
                            !append(result, static_cast<std::uint8_t>(0x80u | (code & 0x3fu)))) return false;
                    }
                    continue;
                }
                default: return false;
                }
            }
            if (!append(result, value)) return false;
        }
        return false;
    }

    bool unsignedInteger(std::uint32_t& result) {
        skipWhitespace();
        if (position_ >= input_.size() || input_[position_] < '0' || input_[position_] > '9') return false;
        if (input_[position_] == '0' && position_ + 1u < input_.size() &&
            input_[position_ + 1u] >= '0' && input_[position_ + 1u] <= '9') return false;
        std::uint64_t value{};
        do {
            value = value * 10u + static_cast<unsigned>(input_[position_] - '0');
            if (value > std::numeric_limits<std::uint32_t>::max()) return false;
            ++position_;
        } while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9');
        result = static_cast<std::uint32_t>(value);
        return true;
    }

    bool skipValue(std::uint8_t depth = 0u) {
        if (depth > maxDepth_) return false;
        skipWhitespace();
        if (position_ >= input_.size()) return false;
        if (input_[position_] == '"') {
            Token ignored{};
            return string(ignored);
        }
        if (input_[position_] == '{') {
            ++position_;
            skipWhitespace();
            if (peek('}')) return consume('}');
            for (;;) {
                Token key{};
                if (!string(key) || !consume(':') || !skipValue(static_cast<std::uint8_t>(depth + 1u))) return false;
                if (consume('}')) return true;
                if (!consume(',')) return false;
            }
        }
        if (input_[position_] == '[') {
            ++position_;
            skipWhitespace();
            if (peek(']')) return consume(']');
            for (;;) {
                if (!skipValue(static_cast<std::uint8_t>(depth + 1u))) return false;
                if (consume(']')) return true;
                if (!consume(',')) return false;
            }
        }
        if (match("true") || match("false") || match("null")) return true;
        return skipNumber();
    }

private:
    bool match(std::string_view value) {
        if (input_.substr(position_, value.size()) != value) return false;
        position_ += value.size();
        return true;
    }

    bool skipNumber() {
        const auto start = position_;
        if (position_ < input_.size() && input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) { position_ = start; return false; }
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' &&
                input_[position_] <= '9') { position_ = start; return false; }
        } else {
            if (input_[position_] < '1' || input_[position_] > '9') {
                position_ = start;
                return false;
            }
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const auto fraction = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
            if (position_ == fraction) { position_ = start; return false; }
        }
        if (position_ < input_.size() &&
            (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const auto exponent = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
            if (position_ == exponent) { position_ = start; return false; }
        }
        return true;
    }

    bool append(Token& token, std::uint8_t value) const {
        if (token.size >= maxStringBytes_) return false;
        token.bytes[token.size++] = static_cast<char>(value);
        return true;
    }

    void skipWhitespace() {
        while (position_ < input_.size()) {
            const auto value = input_[position_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') break;
            ++position_;
        }
    }

    std::string_view input_;
    std::size_t position_{};
    std::uint8_t maxStringBytes_{};
    std::uint8_t maxDepth_{};
};

class Output {
public:
    Output(std::uint8_t* data, std::size_t capacity)
        : data_(data), capacity_(capacity) {
        if (data_ && capacity_ >= packageHeaderSize) {
            std::memset(data_, 0, packageHeaderSize);
            size_ = packageHeaderSize;
        }
    }

    bool ready() const { return data_ && capacity_ >= packageHeaderSize; }
    std::size_t size() const { return size_; }
    std::uint8_t* data() { return data_; }
    const std::uint8_t* data() const { return data_; }

    bool appendRecord(RecordType type, const std::uint8_t* payload, std::size_t payloadSize) {
        const auto total = recordHeaderSize + payloadSize;
        if (total > std::numeric_limits<std::uint16_t>::max() || size_ + total > capacity_) return false;
        data_[size_] = static_cast<std::uint8_t>(type);
        data_[size_ + 1u] = 1u;
        write16(data_ + size_ + 2u, static_cast<std::uint16_t>(total));
        if (payloadSize != 0u) std::memcpy(data_ + size_ + recordHeaderSize, payload, payloadSize);
        size_ += total;
        return true;
    }

    void finish(std::uint16_t sources, std::uint16_t sections, std::uint16_t pages,
                std::uint16_t widgets, std::uint32_t historyBytes,
                std::uint32_t sourceCrc) {
        write32(data_, packageMagic);
        write16(data_ + 4u, packageFormatVersion);
        write16(data_ + 6u, static_cast<std::uint16_t>(packageHeaderSize));
        write32(data_ + 8u, static_cast<std::uint32_t>(size_));
        write32(data_ + 12u, crc32(data_ + packageHeaderSize, size_ - packageHeaderSize));
        write16(data_ + 16u, sources);
        write16(data_ + 18u, sections);
        write16(data_ + 20u, pages);
        write16(data_ + 22u, widgets);
        write32(data_ + 24u, historyBytes);
        write32(data_ + 28u, sourceCrc);
    }

private:
    std::uint8_t* data_{};
    std::size_t capacity_{};
    std::size_t size_{};
};

class CompilerImpl {
public:
    CompilerImpl(std::string_view json, std::uint8_t* output, std::size_t capacity,
                 CompileLimits limits)
        : reader_(json, limits.maxStringBytes, limits.maxJsonDepth),
          output_(output, capacity < limits.maxPackageBytes ? capacity : limits.maxPackageBytes),
          limits_(limits), sourceCrc_(crc32(reinterpret_cast<const std::uint8_t*>(json.data()),
                                            json.size())) {}

    CompileResult run() {
        if (!output_.ready()) return fail(CompileError::outputTooLarge, "package buffer is too small");
        if (!parseRoot() || !reader_.atEnd()) {
            if (result_.error == CompileError::none) return fail(CompileError::invalidJson, "invalid JSON");
            return result_;
        }
        if (!schemaSeen_) return fail(CompileError::missingProperty, "missing schema");
        if (sectionCount_ == 0u) return fail(CompileError::missingProperty, "at least one section is required");
        if (!resolveAndValidate()) return result_;
        output_.finish(sourceCount_, sectionCount_, pageCount_, widgetCount_, historyBytes_,
                       sourceCrc_);
        result_.packageSize = output_.size();
        return result_;
    }

private:
    CompileResult fail(CompileError error, const char* message) {
        if (result_.error == CompileError::none) {
            result_.error = error;
            result_.inputOffset = reader_.position();
            result_.message = message;
        }
        return result_;
    }

    bool keyValuePrefix(Token& key) {
        return reader_.string(key) && reader_.consume(':');
    }

    bool nextObjectMember(bool& done) {
        if (reader_.consume('}')) { done = true; return true; }
        if (!reader_.consume(',')) return false;
        return true;
    }

    bool nextArrayItem(bool& done) {
        if (reader_.consume(']')) { done = true; return true; }
        if (!reader_.consume(',')) return false;
        return true;
    }

    bool parseRoot() {
        if (!reader_.consume('{')) return false;
        if (reader_.consume('}')) return true;
        bool done{};
        while (!done) {
            Token key{};
            if (!keyValuePrefix(key)) return false;
            if (key.equals("schema")) {
                Token schema{};
                if (!reader_.string(schema)) return false;
                if (!schema.equals("semantic-display/v1")) {
                    fail(CompileError::unsupportedSchema, "unsupported configuration schema");
                    return false;
                }
                schemaSeen_ = true;
            } else if (key.equals("permanent_sources")) {
                if (!parseSources(noIndex, true)) return false;
            } else if (key.equals("sections")) {
                if (!parseSections()) return false;
            } else if (!reader_.skipValue()) return false;
            if (!nextObjectMember(done)) return false;
        }
        return true;
    }

    bool parseSources(std::uint16_t sectionIndex, bool permanent) {
        if (!reader_.consume('[')) return false;
        if (reader_.consume(']')) return true;
        bool done{};
        while (!done) {
            if (!parseSource(sectionIndex, permanent)) return false;
            if (!nextArrayItem(done)) return false;
        }
        return true;
    }

    bool parseSubscribe(std::uint32_t& periodMs) {
        if (!reader_.consume('{')) return false;
        if (reader_.consume('}')) return true;
        bool done{};
        while (!done) {
            Token key{};
            if (!keyValuePrefix(key)) return false;
            if (key.equals("period_ms")) {
                if (!reader_.unsignedInteger(periodMs)) return false;
            } else if (!reader_.skipValue()) return false;
            if (!nextObjectMember(done)) return false;
        }
        return true;
    }

    bool parseHistory(std::uint32_t& intervalMs, std::uint16_t& points,
                      HistoryReducer& reducer) {
        if (!reader_.consume('{')) return false;
        if (reader_.consume('}')) return true;
        bool done{};
        while (!done) {
            Token key{};
            if (!keyValuePrefix(key)) return false;
            if (key.equals("interval_ms")) {
                if (!reader_.unsignedInteger(intervalMs)) return false;
            } else if (key.equals("points")) {
                std::uint32_t value{};
                if (!reader_.unsignedInteger(value) || value > 0xffffu) return false;
                points = static_cast<std::uint16_t>(value);
            } else if (key.equals("reducer")) {
                Token value{};
                if (!reader_.string(value)) return false;
                if (value.equals("last")) reducer = HistoryReducer::last;
                else if (value.equals("mean")) reducer = HistoryReducer::mean;
                else if (value.equals("minimum")) reducer = HistoryReducer::minimum;
                else if (value.equals("maximum")) reducer = HistoryReducer::maximum;
                else {
                    fail(CompileError::invalidProperty, "unknown history reducer");
                    return false;
                }
            } else if (!reader_.skipValue()) return false;
            if (!nextObjectMember(done)) return false;
        }
        return true;
    }

    bool parseSource(std::uint16_t sectionIndex, bool permanent) {
        if (!reader_.consume('{')) return false;
        Token id{}, provider{}, path{}, unit{};
        std::memcpy(provider.bytes, "signalk", 7u); provider.size = 7u;
        std::uint32_t subscribeMs{}, staleMs{}, historyInterval{};
        std::uint16_t historyPoints{};
        auto reducer = HistoryReducer::none;
        if (!reader_.consume('}')) {
            bool done{};
            while (!done) {
                Token key{};
                if (!keyValuePrefix(key)) return false;
                if (key.equals("id")) { if (!reader_.string(id)) return false; }
                else if (key.equals("provider")) { if (!reader_.string(provider)) return false; }
                else if (key.equals("path")) { if (!reader_.string(path)) return false; }
                else if (key.equals("unit")) { if (!reader_.string(unit)) return false; }
                else if (key.equals("stale_ms")) { if (!reader_.unsignedInteger(staleMs)) return false; }
                else if (key.equals("subscribe")) { if (!parseSubscribe(subscribeMs)) return false; }
                else if (key.equals("history")) {
                    if (!parseHistory(historyInterval, historyPoints, reducer)) return false;
                } else if (!reader_.skipValue()) return false;
                if (!nextObjectMember(done)) return false;
            }
        }
        if (id.size == 0u || path.size == 0u) {
            fail(CompileError::missingProperty, "source requires id and path");
            return false;
        }
        if ((historyPoints == 0u) != (historyInterval == 0u)) {
            fail(CompileError::invalidProperty, "history requires interval_ms and points");
            return false;
        }
        if (historyPoints != 0u && reducer == HistoryReducer::none) reducer = HistoryReducer::last;
        const auto addedHistory = historyPoints == 0u ? 0u :
            static_cast<std::uint32_t>(historySeriesOverheadBytes) +
            static_cast<std::uint32_t>(historyPoints) * sizeof(float);
        if (addedHistory > limits_.maxHistoryBytes - historyBytes_) {
            fail(CompileError::historyBudgetExceeded, "configuration exceeds history RAM budget");
            return false;
        }
        if (sourceCount_ == noIndex || sourceCount_ >= limits_.maxSources) {
            fail(CompileError::dataBudgetExceeded, "configuration exceeds data-slot budget");
            return false;
        }
        std::uint8_t payload[24u + 4u * 255u]{};
        auto* cursor = payload;
        write16(cursor, sectionIndex); cursor += 2u;
        write16(cursor, sourceCount_); cursor += 2u;
        write32(cursor, subscribeMs); cursor += 4u;
        write32(cursor, staleMs); cursor += 4u;
        write32(cursor, historyInterval); cursor += 4u;
        write16(cursor, historyPoints); cursor += 2u;
        *cursor++ = static_cast<std::uint8_t>(reducer);
        *cursor++ = permanent ? 1u : 0u;
        *cursor++ = provider.size;
        *cursor++ = id.size;
        *cursor++ = path.size;
        *cursor++ = unit.size;
        auto append = [&cursor](const Token& token) {
            std::memcpy(cursor, token.bytes, token.size); cursor += token.size;
        };
        append(provider); append(id); append(path); append(unit);
        if (!output_.appendRecord(RecordType::source, payload,
                                  static_cast<std::size_t>(cursor - payload))) {
            fail(CompileError::outputTooLarge, "compiled configuration exceeds package limit");
            return false;
        }
        historyBytes_ += addedHistory;
        ++sourceCount_;
        return true;
    }

    bool parseSections() {
        if (!reader_.consume('[')) return false;
        if (reader_.consume(']')) return true;
        bool done{};
        while (!done) {
            if (!parseSection()) return false;
            if (!nextArrayItem(done)) return false;
        }
        return true;
    }

    bool parseButtonLabels(Token& previous, Token& next, Token& action1, Token& action2) {
        if (!reader_.consume('{')) return false;
        if (reader_.consume('}')) return true;
        bool done{};
        while (!done) {
            Token key{};
            if (!keyValuePrefix(key)) return false;
            if (key.equals("previous_page")) { if (!reader_.string(previous)) return false; }
            else if (key.equals("next_page")) { if (!reader_.string(next)) return false; }
            else if (key.equals("action_1")) { if (!reader_.string(action1)) return false; }
            else if (key.equals("action_2")) { if (!reader_.string(action2)) return false; }
            else if (!reader_.skipValue()) return false;
            if (!nextObjectMember(done)) return false;
        }
        return true;
    }

    bool parseSection() {
        if (sectionCount_ == noIndex || !reader_.consume('{')) return false;
        const auto sectionIndex = sectionCount_;
        Token id{}, title{}, previous{}, next{}, action1{}, action2{};
        bool showButtonLabels{};
        if (!reader_.consume('}')) {
            bool done{};
            while (!done) {
                Token key{};
                if (!keyValuePrefix(key)) return false;
                if (key.equals("id")) { if (!reader_.string(id)) return false; }
                else if (key.equals("title")) { if (!reader_.string(title)) return false; }
                else if (key.equals("button_labels")) {
                    if (!parseButtonLabels(previous, next, action1, action2)) return false;
                    showButtonLabels = true;
                }
                else if (key.equals("sources")) {
                    if (!parseSources(sectionIndex, false)) return false;
                } else if (key.equals("pages")) {
                    if (!parsePages(sectionIndex)) return false;
                } else if (!reader_.skipValue()) return false;
                if (!nextObjectMember(done)) return false;
            }
        }
        if (id.size == 0u) {
            fail(CompileError::missingProperty, "section requires id");
            return false;
        }
        if (title.size == 0u) title = id;
        std::uint8_t payload[9u + 6u * 255u]{};
        write16(payload, sectionIndex);
        payload[2] = id.size;
        payload[3] = title.size;
        auto* cursor = payload + 4u;
        std::memcpy(cursor, id.bytes, id.size); cursor += id.size;
        std::memcpy(cursor, title.bytes, title.size); cursor += title.size;
        *cursor++ = showButtonLabels ? 1u : 0u;
        *cursor++ = previous.size;
        *cursor++ = next.size;
        *cursor++ = action1.size;
        *cursor++ = action2.size;
        auto append = [&cursor](const Token& token) {
            std::memcpy(cursor, token.bytes, token.size); cursor += token.size;
        };
        append(previous); append(next); append(action1); append(action2);
        if (!output_.appendRecord(RecordType::section, payload,
                                  static_cast<std::size_t>(cursor - payload))) {
            fail(CompileError::outputTooLarge, "compiled configuration exceeds package limit");
            return false;
        }
        ++sectionCount_;
        return true;
    }

    bool parsePages(std::uint16_t sectionIndex) {
        if (!reader_.consume('[')) return false;
        if (reader_.consume(']')) return true;
        bool done{};
        while (!done) {
            if (!parsePage(sectionIndex)) return false;
            if (!nextArrayItem(done)) return false;
        }
        return true;
    }

    bool parseGrid(std::uint8_t& columns, std::uint8_t& rows, std::uint8_t& gap) {
        if (!reader_.consume('{')) return false;
        if (reader_.consume('}')) return true;
        bool done{};
        while (!done) {
            Token key{};
            if (!keyValuePrefix(key)) return false;
            std::uint32_t value{};
            if (key.equals("columns") || key.equals("rows") || key.equals("gap")) {
                if (!reader_.unsignedInteger(value) || value > 255u) return false;
                if (key.equals("columns")) columns = static_cast<std::uint8_t>(value);
                else if (key.equals("rows")) rows = static_cast<std::uint8_t>(value);
                else gap = static_cast<std::uint8_t>(value);
            } else if (!reader_.skipValue()) return false;
            if (!nextObjectMember(done)) return false;
        }
        return true;
    }

    bool parsePage(std::uint16_t sectionIndex) {
        if (pageCount_ == noIndex || !reader_.consume('{')) return false;
        const auto pageIndex = pageCount_;
        Token id{}, title{};
        std::uint8_t columns{}, rows{}, gap{};
        if (!reader_.consume('}')) {
            bool done{};
            while (!done) {
                Token key{};
                if (!keyValuePrefix(key)) return false;
                if (key.equals("id")) { if (!reader_.string(id)) return false; }
                else if (key.equals("title")) { if (!reader_.string(title)) return false; }
                else if (key.equals("grid")) { if (!parseGrid(columns, rows, gap)) return false; }
                else if (key.equals("widgets")) {
                    if (!parseWidgets(sectionIndex, pageIndex)) return false;
                } else if (!reader_.skipValue()) return false;
                if (!nextObjectMember(done)) return false;
            }
        }
        if (id.size == 0u || columns == 0u || rows == 0u) {
            fail(CompileError::missingProperty, "page requires id and non-zero grid dimensions");
            return false;
        }
        if (title.size == 0u) title = id;
        std::uint8_t payload[9u + 2u * 255u]{};
        write16(payload, sectionIndex);
        write16(payload + 2u, pageIndex);
        payload[4] = columns;
        payload[5] = rows;
        payload[6] = gap;
        payload[7] = id.size;
        payload[8] = title.size;
        auto* cursor = payload + 9u;
        std::memcpy(cursor, id.bytes, id.size); cursor += id.size;
        std::memcpy(cursor, title.bytes, title.size); cursor += title.size;
        if (!output_.appendRecord(RecordType::page, payload,
                                  static_cast<std::size_t>(cursor - payload))) {
            fail(CompileError::outputTooLarge, "compiled configuration exceeds package limit");
            return false;
        }
        ++pageCount_;
        return true;
    }

    bool parseWidgets(std::uint16_t sectionIndex, std::uint16_t pageIndex) {
        if (!reader_.consume('[')) return false;
        if (reader_.consume(']')) return true;
        bool done{};
        while (!done) {
            if (!parseWidget(sectionIndex, pageIndex)) return false;
            if (!nextArrayItem(done)) return false;
        }
        return true;
    }

    bool parseCell(std::uint8_t& column, std::uint8_t& row,
                   std::uint8_t& columnSpan, std::uint8_t& rowSpan) {
        if (!reader_.consume('{')) return false;
        if (reader_.consume('}')) return true;
        bool done{};
        while (!done) {
            Token key{};
            if (!keyValuePrefix(key)) return false;
            std::uint32_t value{};
            if (key.equals("column") || key.equals("row") || key.equals("column_span") ||
                key.equals("row_span")) {
                if (!reader_.unsignedInteger(value) || value > 255u) return false;
                if (key.equals("column")) column = static_cast<std::uint8_t>(value);
                else if (key.equals("row")) row = static_cast<std::uint8_t>(value);
                else if (key.equals("column_span")) columnSpan = static_cast<std::uint8_t>(value);
                else rowSpan = static_cast<std::uint8_t>(value);
            } else if (!reader_.skipValue()) return false;
            if (!nextObjectMember(done)) return false;
        }
        return true;
    }

    bool parseWidget(std::uint16_t sectionIndex, std::uint16_t pageIndex) {
        if (widgetCount_ == noIndex || !reader_.consume('{')) return false;
        Token type{}, source{}, label{}, unit{}, text{};
        std::uint8_t column{}, row{}, columnSpan{1u}, rowSpan{1u};
        std::uint32_t decimals{};
        std::uint32_t maxDigits{defaultValueMaxDigits};
        if (!reader_.consume('}')) {
            bool done{};
            while (!done) {
                Token key{};
                if (!keyValuePrefix(key)) return false;
                if (key.equals("type")) { if (!reader_.string(type)) return false; }
                else if (key.equals("source")) { if (!reader_.string(source)) return false; }
                else if (key.equals("label")) { if (!reader_.string(label)) return false; }
                else if (key.equals("display_unit")) { if (!reader_.string(unit)) return false; }
                else if (key.equals("text")) { if (!reader_.string(text)) return false; }
                else if (key.equals("decimals")) {
                    if (!reader_.unsignedInteger(decimals) || decimals > 6u) return false;
                } else if (key.equals("max_digits")) {
                    if (!reader_.unsignedInteger(maxDigits) || maxDigits == 0u ||
                        maxDigits > maximumValueMaxDigits) return false;
                } else if (key.equals("cell")) {
                    if (!parseCell(column, row, columnSpan, rowSpan)) return false;
                } else if (!reader_.skipValue()) return false;
                if (!nextObjectMember(done)) return false;
            }
        }
        if (type.size == 0u || columnSpan == 0u || rowSpan == 0u) {
            fail(CompileError::missingProperty, "widget requires type and non-zero cell span");
            return false;
        }
        WidgetType widgetType = WidgetType::unknown;
        Token storedType = type;
        if (type.equals("text")) { widgetType = WidgetType::text; storedType.size = 0u; }
        else if (type.equals("value")) { widgetType = WidgetType::value; storedType.size = 0u; }
        else if (type.equals("local-clock")) { widgetType = WidgetType::localClock; storedType.size = 0u; }
        if (widgetType == WidgetType::value && source.size == 0u) {
            fail(CompileError::missingProperty, "value widget requires source");
            return false;
        }
        if (widgetType == WidgetType::text && text.size == 0u) {
            fail(CompileError::missingProperty, "text widget requires text");
            return false;
        }
        std::uint8_t payload[18u + 5u * 255u]{};
        write16(payload, sectionIndex);
        write16(payload + 2u, pageIndex);
        write16(payload + 4u, noIndex);
        payload[6] = static_cast<std::uint8_t>(widgetType);
        payload[7] = column;
        payload[8] = row;
        payload[9] = columnSpan;
        payload[10] = rowSpan;
        payload[11] = static_cast<std::uint8_t>(decimals);
        payload[12] = storedType.size;
        payload[13] = source.size;
        payload[14] = label.size;
        payload[15] = unit.size;
        payload[16] = text.size;
        payload[17] = static_cast<std::uint8_t>(maxDigits);
        auto* cursor = payload + 18u;
        auto append = [&cursor](const Token& token) {
            std::memcpy(cursor, token.bytes, token.size); cursor += token.size;
        };
        append(storedType); append(source); append(label); append(unit); append(text);
        if (!output_.appendRecord(RecordType::widget, payload,
                                  static_cast<std::size_t>(cursor - payload))) {
            fail(CompileError::outputTooLarge, "compiled configuration exceeds package limit");
            return false;
        }
        ++widgetCount_;
        return true;
    }

    bool sourceIdentity(std::size_t recordOffset, std::uint16_t& sectionIndex,
                        std::uint16_t& sourceIndex, std::string_view& id,
                        bool& permanent) const {
        const auto* record = output_.data() + recordOffset;
        const auto size = read16(record + 2u);
        if (record[0] != static_cast<std::uint8_t>(RecordType::source) || size < 28u) return false;
        const auto* payload = record + 4u;
        sectionIndex = read16(payload);
        sourceIndex = read16(payload + 2u);
        permanent = (payload[19] & 1u) != 0u;
        const auto providerLength = payload[20];
        const auto idLength = payload[21];
        const auto stringStart = payload + 24u;
        if (24u + providerLength + idLength > size - 4u) return false;
        id = {reinterpret_cast<const char*>(stringStart + providerLength), idLength};
        return true;
    }

    bool findSource(std::uint16_t sectionIndex, std::string_view id,
                    std::uint16_t& sourceIndex) const {
        bool foundPermanent{};
        std::uint16_t permanentIndex{noIndex};
        for (std::size_t offset = packageHeaderSize; offset < output_.size();) {
            const auto* record = output_.data() + offset;
            const auto recordSize = read16(record + 2u);
            std::uint16_t candidateSection{}, candidateIndex{};
            std::string_view candidateId{};
            bool permanent{};
            if (sourceIdentity(offset, candidateSection, candidateIndex, candidateId, permanent) &&
                candidateId == id) {
                if (candidateSection == sectionIndex && !permanent) {
                    sourceIndex = candidateIndex;
                    return true;
                }
                if (permanent) { foundPermanent = true; permanentIndex = candidateIndex; }
            }
            offset += recordSize;
        }
        if (foundPermanent) sourceIndex = permanentIndex;
        return foundPermanent;
    }

    bool pageGrid(std::uint16_t pageIndex, std::uint8_t& columns, std::uint8_t& rows) const {
        for (std::size_t offset = packageHeaderSize; offset < output_.size();) {
            const auto* record = output_.data() + offset;
            const auto recordSize = read16(record + 2u);
            if (record[0] == static_cast<std::uint8_t>(RecordType::page) && recordSize >= 13u) {
                const auto* payload = record + 4u;
                if (read16(payload + 2u) == pageIndex) {
                    columns = payload[4]; rows = payload[5];
                    return true;
                }
            }
            offset += recordSize;
        }
        return false;
    }

    bool sectionIdentity(std::size_t offset, std::uint16_t& sectionIndex,
                         std::string_view& id) const {
        const auto* record = output_.data() + offset;
        const auto recordSize = read16(record + 2u);
        if (record[0] != static_cast<std::uint8_t>(RecordType::section) || recordSize < 8u) {
            return false;
        }
        const auto* payload = record + 4u;
        const auto idLength = payload[2];
        const auto titleLength = payload[3];
        if (4u + idLength + titleLength > recordSize - 4u) return false;
        sectionIndex = read16(payload);
        id = {reinterpret_cast<const char*>(payload + 4u), idLength};
        return true;
    }

    bool pageIdentity(std::size_t offset, std::uint16_t& sectionIndex,
                      std::uint16_t& pageIndex, std::string_view& id) const {
        const auto* record = output_.data() + offset;
        const auto recordSize = read16(record + 2u);
        if (record[0] != static_cast<std::uint8_t>(RecordType::page) || recordSize < 13u) {
            return false;
        }
        const auto* payload = record + 4u;
        const auto idLength = payload[7];
        const auto titleLength = payload[8];
        if (9u + idLength + titleLength > recordSize - 4u) return false;
        sectionIndex = read16(payload);
        pageIndex = read16(payload + 2u);
        id = {reinterpret_cast<const char*>(payload + 9u), idLength};
        return true;
    }

    bool duplicateIdentifiers() {
        for (std::size_t left = packageHeaderSize; left < output_.size();) {
            const auto leftSize = read16(output_.data() + left + 2u);
            std::uint16_t leftSection{}, leftIndex{};
            std::string_view leftId{};
            bool leftPermanent{};
            const bool leftSource = sourceIdentity(left, leftSection, leftIndex, leftId,
                                                   leftPermanent);
            const bool leftSectionRecord = sectionIdentity(left, leftSection, leftId);
            const bool leftPage = pageIdentity(left, leftSection, leftIndex, leftId);
            if (leftSource || leftSectionRecord || leftPage) {
                for (std::size_t right = left + leftSize; right < output_.size();) {
                    const auto rightSize = read16(output_.data() + right + 2u);
                    std::uint16_t rightSection{}, rightIndex{};
                    std::string_view rightId{};
                    bool rightPermanent{};
                    bool duplicate{};
                    if (leftSource && sourceIdentity(right, rightSection, rightIndex, rightId,
                                                     rightPermanent)) {
                        duplicate = leftId == rightId && leftPermanent == rightPermanent &&
                            (leftPermanent || leftSection == rightSection);
                    } else if (leftSectionRecord &&
                               sectionIdentity(right, rightSection, rightId)) {
                        duplicate = leftId == rightId;
                    } else if (leftPage &&
                               pageIdentity(right, rightSection, rightIndex, rightId)) {
                        duplicate = leftSection == rightSection && leftId == rightId;
                    }
                    if (duplicate) {
                        fail(CompileError::duplicateId, "duplicate id in configuration scope");
                        return true;
                    }
                    right += rightSize;
                }
            }
            left += leftSize;
        }
        return false;
    }

    bool widgetsOverlap() {
        for (std::size_t left = packageHeaderSize; left < output_.size();) {
            const auto* leftRecord = output_.data() + left;
            const auto leftSize = read16(leftRecord + 2u);
            if (leftRecord[0] == static_cast<std::uint8_t>(RecordType::widget) &&
                leftSize >= 22u) {
                const auto* a = leftRecord + 4u;
                for (std::size_t right = left + leftSize; right < output_.size();) {
                    const auto* rightRecord = output_.data() + right;
                    const auto rightSize = read16(rightRecord + 2u);
                    if (rightRecord[0] == static_cast<std::uint8_t>(RecordType::widget) &&
                        rightSize >= 22u) {
                        const auto* b = rightRecord + 4u;
                        const bool samePage = read16(a + 2u) == read16(b + 2u);
                        const bool columnsOverlap = a[7] < static_cast<unsigned>(b[7]) + b[9] &&
                            b[7] < static_cast<unsigned>(a[7]) + a[9];
                        const bool rowsOverlap = a[8] < static_cast<unsigned>(b[8]) + b[10] &&
                            b[8] < static_cast<unsigned>(a[8]) + a[10];
                        if (samePage && columnsOverlap && rowsOverlap) {
                            fail(CompileError::invalidLayout,
                                 "widgets overlap in the same page grid");
                            return true;
                        }
                    }
                    right += rightSize;
                }
            }
            left += leftSize;
        }
        return false;
    }

    bool resolveAndValidate() {
        if (duplicateIdentifiers()) return false;
        for (std::size_t offset = packageHeaderSize; offset < output_.size();) {
            auto* record = output_.data() + offset;
            const auto recordSize = read16(record + 2u);
            if (recordSize < 4u || offset + recordSize > output_.size()) {
                fail(CompileError::invalidProperty, "invalid compiled record");
                return false;
            }
            if (record[0] == static_cast<std::uint8_t>(RecordType::widget)) {
                auto* payload = record + 4u;
                if (recordSize < 22u) return false;
                const auto sectionIndex = read16(payload);
                const auto pageIndex = read16(payload + 2u);
                const auto sourceLength = payload[13];
                const auto typeLength = payload[12];
                const auto* sourceStart = payload + 18u + typeLength;
                if (sourceLength != 0u) {
                    std::uint16_t sourceIndex{};
                    const std::string_view sourceId{reinterpret_cast<const char*>(sourceStart), sourceLength};
                    if (!findSource(sectionIndex, sourceId, sourceIndex)) {
                        fail(CompileError::unresolvedSource, "widget references an unknown source");
                        return false;
                    }
                    write16(payload + 4u, sourceIndex);
                }
                std::uint8_t columns{}, rows{};
                if (!pageGrid(pageIndex, columns, rows) || payload[9] == 0u || payload[10] == 0u ||
                    static_cast<unsigned>(payload[7]) + payload[9] > columns ||
                    static_cast<unsigned>(payload[8]) + payload[10] > rows) {
                    fail(CompileError::invalidLayout, "widget is outside its page grid");
                    return false;
                }
            }
            offset += recordSize;
        }
        if (widgetsOverlap()) return false;
        return true;
    }

    JsonReader reader_;
    Output output_;
    CompileLimits limits_;
    CompileResult result_{};
    std::uint16_t sourceCount_{};
    std::uint16_t sectionCount_{};
    std::uint16_t pageCount_{};
    std::uint16_t widgetCount_{};
    std::uint32_t historyBytes_{};
    std::uint32_t sourceCrc_{};
    bool schemaSeen_{};
};

} // namespace

CompileResult ConfigCompiler::compile(std::string_view json, std::uint8_t* output,
                                      std::size_t capacity) const {
    if (json.size() > limits_.maxSourceBytes) {
        return {CompileError::sourceTooLarge, 0u, 0u, "source JSON exceeds configured limit"};
    }
    return CompilerImpl{json, output, capacity, limits_}.run();
}

} // namespace semantic_display
