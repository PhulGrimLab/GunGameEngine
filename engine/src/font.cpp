#include "font.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include "obj_loader.h"   // Utf8ToWide

namespace gge {

namespace {

[[noreturn]] void Fail(int line, const std::string& what) { throw std::runtime_error("line " + std::to_string(line) + ": " + what); }

// 공백으로 나뉜 토큰 하나를 떼어낸다.
std::string_view NextToken(std::string_view& s)
{
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    size_t j = i;
    while (j < s.size() && s[j] != ' ' && s[j] != '\t')
        ++j;
    std::string_view token = s.substr(i, j - i);
    s.remove_prefix(j);
    return token;
}

// 십진 정수 하나. 부호 허용, 자릿수 제한으로 오버플로를 막는다.
bool ParseInt(std::string_view token, long long& out)
{
    if (token.empty() || token.size() > 12)
        return false;
    size_t i = 0;
    bool negative = false;
    if (token[0] == '-' || token[0] == '+') {
        negative = token[0] == '-';
        i = 1;
    }
    if (i == token.size())
        return false;
    long long value = 0;
    for (; i < token.size(); ++i) {
        if (token[i] < '0' || token[i] > '9')
            return false;
        value = value * 10 + (token[i] - '0');
    }
    out = negative ? -value : value;
    return true;
}

long long RequireInt(std::string_view& rest, int line, const char* what, long long min, long long max)
{
    long long value = 0;
    if (!ParseInt(NextToken(rest), value))
        Fail(line, std::string(what) + " must be an integer");
    if (value < min || value > max)
        Fail(line, std::string(what) + " " + std::to_string(value) + " is out of range (" + std::to_string(min) + ".." + std::to_string(max) + ")");
    return value;
}

int HexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

}  // namespace

Font Font::Parse(std::string_view text)
{
    if (text.size() > kMaxFontFileBytes)
        throw std::runtime_error("font file is too large (max " + std::to_string(kMaxFontFileBytes) + " bytes)");

    Font font;
    size_t pos = 0;
    int lineNo = 0;
    auto nextLine = [&](std::string_view& line) {
        if (pos >= text.size())
            return false;
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        line = text.substr(pos, end - pos);
        pos = end < text.size() ? end + 1 : end;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        ++lineNo;
        for (char c : line)
            if (static_cast<unsigned char>(c) < 0x20 && c != '\t')
                Fail(lineNo, "unexpected control character in the font file");
        return true;
    };

    std::string_view line;
    if (!nextLine(line))
        throw std::runtime_error("the font file is empty");
    {
        std::string_view rest = line;
        if (NextToken(rest) != "STARTFONT")
            Fail(lineNo, "a BDF font must start with STARTFONT");
    }

    long long declaredGlyphs = -1, ascent = -1, descent = -1, encodedGlyphs = 0;
    long long boxHeight = 0, boxY = 0;
    bool sawBox = false, ended = false;
    while (nextLine(line)) {
        std::string_view rest = line;
        const std::string_view key = NextToken(rest);
        if (key == "FONTBOUNDINGBOX") {
            RequireInt(rest, lineNo, "FONTBOUNDINGBOX width", 0, kMaxGlyphSide * 4);
            boxHeight = RequireInt(rest, lineNo, "FONTBOUNDINGBOX height", 0, kMaxFontMetric * 2);
            RequireInt(rest, lineNo, "FONTBOUNDINGBOX x offset", -kMaxFontMetric, kMaxFontMetric);
            boxY = RequireInt(rest, lineNo, "FONTBOUNDINGBOX y offset", -kMaxFontMetric, kMaxFontMetric);
            sawBox = true;
        } else if (key == "FONT_ASCENT") {
            ascent = RequireInt(rest, lineNo, "FONT_ASCENT", 0, kMaxFontMetric);
        } else if (key == "FONT_DESCENT") {
            descent = RequireInt(rest, lineNo, "FONT_DESCENT", 0, kMaxFontMetric);
        } else if (key == "CHARS") {
            if (declaredGlyphs >= 0)
                Fail(lineNo, "CHARS appears twice");
            declaredGlyphs = RequireInt(rest, lineNo, "CHARS", 0, kMaxFontGlyphs);
        } else if (key == "ENDFONT") {
            ended = true;
            break;
        } else if (key == "STARTCHAR") {
            if (declaredGlyphs < 0)
                Fail(lineNo, "CHARS must come before the first glyph");
            // 글리프 블록: ENCODING / (SWIDTH) / DWIDTH / BBX / BITMAP + 줄 / ENDCHAR
            long long encoding = 0, advance = 0, w = 0, h = 0, xo = 0, yo = 0;
            bool hasEncoding = false, hasAdvance = false, hasBox = false;
            const int startLine = lineNo;
            const std::string name(NextToken(rest));
            bool done = false;
            while (!done) {
                if (!nextLine(line))
                    Fail(lineNo, "the font file ends inside the glyph that starts at line " + std::to_string(startLine));
                std::string_view r = line;
                const std::string_view k = NextToken(r);
                if (k == "ENCODING") {
                    encoding = RequireInt(r, lineNo, "ENCODING", -1, 0x10FFFF);
                    hasEncoding = true;
                    if (encoding >= 0xD800 && encoding <= 0xDFFF)
                        Fail(lineNo, "ENCODING " + std::to_string(encoding) + " is a surrogate, not a character");
                } else if (k == "DWIDTH") {
                    advance = RequireInt(r, lineNo, "DWIDTH", 0, kMaxGlyphAdvance);
                    hasAdvance = true;
                } else if (k == "BBX") {
                    w = RequireInt(r, lineNo, "BBX width", 0, kMaxGlyphSide);
                    h = RequireInt(r, lineNo, "BBX height", 0, kMaxGlyphSide);
                    xo = RequireInt(r, lineNo, "BBX x offset", -kMaxFontMetric, kMaxFontMetric);
                    yo = RequireInt(r, lineNo, "BBX y offset", -kMaxFontMetric, kMaxFontMetric);
                    hasBox = true;
                } else if (k == "BITMAP") {
                    if (!hasEncoding || !hasAdvance || !hasBox)
                        Fail(lineNo, "a glyph needs ENCODING, DWIDTH and BBX before BITMAP (glyph at line " + std::to_string(startLine) + ")");
                    const size_t bytesPerRow = static_cast<size_t>((w + 7) / 8);
                    const size_t bitsStart = font._bits.size();
                    for (long long row = 0; row < h; ++row) {
                        if (!nextLine(line))
                            Fail(lineNo, "the font file ends inside a bitmap");
                        if (line.size() != bytesPerRow * 2)
                            Fail(lineNo, "bitmap row has " + std::to_string(line.size()) + " hex digits, expected " + std::to_string(bytesPerRow * 2) +
                                             " for a glyph " + std::to_string(w) + " pixels wide");
                        for (size_t i = 0; i < bytesPerRow; ++i) {
                            const int hi = HexValue(line[i * 2]), lo = HexValue(line[i * 2 + 1]);
                            if (hi < 0 || lo < 0)
                                Fail(lineNo, "bitmap rows must be hexadecimal");
                            font._bits.push_back(static_cast<uint8_t>(hi * 16 + lo));
                        }
                    }
                    if (!nextLine(line))
                        Fail(lineNo, "the font file ends before ENDCHAR");
                    std::string_view e = line;
                    if (NextToken(e) != "ENDCHAR")
                        Fail(lineNo, "expected ENDCHAR after " + std::to_string(h) + " bitmap rows (more rows than BBX height?)");
                    done = true;

                    Glyph g;
                    g.advance = static_cast<int>(advance);
                    g.width = static_cast<int>(w);
                    g.height = static_cast<int>(h);
                    g.xOffset = static_cast<int>(xo);
                    g.yOffset = static_cast<int>(yo);
                    g.bitsOffset = bitsStart;
                    if (encoding == -1) {
                        if (name == ".notdef" && font._notdef < 0) {
                            font._notdef = static_cast<int>(font._glyphs.size());
                            font._glyphs.push_back(g);
                        } else {
                            font._bits.resize(bitsStart);   // 코드 포인트가 없는 다른 글리프는 쓰지 않는다
                            font._glyphs.push_back(g);      // CHARS 개수에는 센다
                            font._glyphs.back().width = 0;
                            font._glyphs.back().height = 0;
                        }
                    } else {
                        const uint32_t cp = static_cast<uint32_t>(encoding);
                        if (!font._index.emplace(cp, static_cast<uint32_t>(font._glyphs.size())).second)
                            Fail(startLine, "duplicate glyph for U+" + std::to_string(cp));
                        font._glyphs.push_back(g);
                    }
                    if (encoding >= 0)
                        ++encodedGlyphs;
                    // CHARS는 코드 포인트가 있는 글리프 수로 세는 파일(Galmuri: .notdef는 세지 않는다)도, 전부 세는 파일도 있다.
                    if (encodedGlyphs > declaredGlyphs || static_cast<long long>(font._glyphs.size()) > declaredGlyphs + 1)
                        Fail(startLine, "the font has more glyphs than CHARS declares (" + std::to_string(declaredGlyphs) + ")");
                } else if (k == "ENDCHAR") {
                    Fail(lineNo, "ENDCHAR without BITMAP");
                } else if (k == "STARTCHAR" || k == "ENDFONT") {
                    Fail(lineNo, "the glyph that starts at line " + std::to_string(startLine) + " is not closed with ENDCHAR");
                }
                // 그 밖의 줄(SWIDTH, ATTRIBUTES 등)은 쓰지 않는다
            }
        }
        // 그 밖의 줄(속성 등)은 쓰지 않는다
    }
    if (!ended)
        throw std::runtime_error("the font file ends before ENDFONT (truncated?)");
    if (declaredGlyphs < 0)
        throw std::runtime_error("the font has no CHARS line");
    if (declaredGlyphs != encodedGlyphs && declaredGlyphs != static_cast<long long>(font._glyphs.size()))
        throw std::runtime_error("CHARS declares " + std::to_string(declaredGlyphs) + " glyphs but the file has " + std::to_string(encodedGlyphs) +
                                 " with a character code (" + std::to_string(font._glyphs.size()) + " in all)");

    if (ascent >= 0 && descent >= 0) {
        font._ascent = static_cast<int>(ascent);
        font._descent = static_cast<int>(descent);
    } else if (sawBox) {
        font._ascent = static_cast<int>(std::max(0LL, boxHeight + boxY));
        font._descent = static_cast<int>(std::max(0LL, -boxY));
    } else {
        throw std::runtime_error("the font has neither FONT_ASCENT/FONT_DESCENT nor FONTBOUNDINGBOX");
    }
    if (font.LineHeight() <= 0)
        throw std::runtime_error("the font has no line height (ascent + descent is 0)");
    return font;
}

Font Font::LoadFile(const std::string& pathUtf8)
{
    std::ifstream file(Utf8ToWide(pathUtf8), std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("cannot open font file '" + pathUtf8 + "'");
    const std::streamoff size = file.tellg();
    if (size < 0 || static_cast<unsigned long long>(size) > kMaxFontFileBytes)
        throw std::runtime_error("font file '" + pathUtf8 + "' is too large (max " + std::to_string(kMaxFontFileBytes) + " bytes)");
    std::string text(static_cast<size_t>(size), '\0');
    file.seekg(0);
    file.read(text.data(), size);
    if (!file)
        throw std::runtime_error("cannot read font file '" + pathUtf8 + "'");
    try {
        return Parse(text);
    } catch (const std::exception& e) {
        throw std::runtime_error("font file '" + pathUtf8 + "': " + e.what());
    }
}

const Glyph* Font::Find(uint32_t codePoint) const
{
    const auto it = _index.find(codePoint);
    return it == _index.end() ? nullptr : &_glyphs[it->second];
}

const Glyph& Font::Resolve(uint32_t codePoint) const
{
    if (const Glyph* g = Find(codePoint))
        return *g;
    return _notdef >= 0 ? _glyphs[static_cast<size_t>(_notdef)] : _empty;
}

bool Font::Pixel(const Glyph& glyph, int x, int y) const
{
    if (x < 0 || y < 0 || x >= glyph.width || y >= glyph.height)
        return false;
    const size_t bytesPerRow = static_cast<size_t>((glyph.width + 7) / 8);
    const uint8_t byte = _bits[glyph.bitsOffset + static_cast<size_t>(y) * bytesPerRow + static_cast<size_t>(x / 8)];
    return (byte >> (7 - x % 8)) & 1;
}

std::vector<uint32_t> DecodeUtf8(std::string_view text)
{
    std::vector<uint32_t> out;
    out.reserve(text.size());
    uint32_t codePoint = 0;
    int needed = 0, seen = 0;
    uint8_t lower = 0x80, upper = 0xBF;
    for (size_t i = 0; i < text.size();) {
        const uint8_t b = static_cast<uint8_t>(text[i]);
        if (needed == 0) {
            ++i;
            if (b <= 0x7F) {
                out.push_back(b);
            } else if (b >= 0xC2 && b <= 0xDF) {
                needed = 1;
                codePoint = b & 0x1F;
            } else if (b >= 0xE0 && b <= 0xEF) {
                if (b == 0xE0) lower = 0xA0;
                if (b == 0xED) upper = 0x9F;
                needed = 2;
                codePoint = b & 0x0F;
            } else if (b >= 0xF0 && b <= 0xF4) {
                if (b == 0xF0) lower = 0x90;
                if (b == 0xF4) upper = 0x8F;
                needed = 3;
                codePoint = b & 0x07;
            } else {
                out.push_back(kReplacementChar);
            }
            continue;
        }
        if (b < lower || b > upper) {
            // 이어지는 바이트가 아니다: 지금까지의 부분 열은 U+FFFD 하나이고, 이 바이트는 다시 처음부터 읽는다.
            codePoint = 0;
            needed = seen = 0;
            lower = 0x80;
            upper = 0xBF;
            out.push_back(kReplacementChar);
            continue;
        }
        lower = 0x80;
        upper = 0xBF;
        codePoint = (codePoint << 6) | (b & 0x3F);
        ++seen;
        ++i;
        if (seen == needed) {
            out.push_back(codePoint);
            codePoint = 0;
            needed = seen = 0;
        }
    }
    if (needed != 0)
        out.push_back(kReplacementChar);   // 잘린 끝
    return out;
}

namespace {

bool IsIgnoredControl(uint32_t c) { return (c < 0x20 && c != '\n') || (c >= 0x7F && c <= 0x9F); }

}  // namespace

TextLayout LayoutText(const Font& font, std::string_view utf8, int maxWidth)
{
    if (utf8.size() > kMaxTextBytes)
        throw std::runtime_error("text is too long (" + std::to_string(utf8.size()) + " bytes, max " + std::to_string(kMaxTextBytes) + ")");
    TextLayout layout;
    for (uint32_t c : DecodeUtf8(utf8)) {
        if (!IsIgnoredControl(c))
            layout.chars.push_back(c);
    }
    if (layout.chars.empty())
        return layout;

    auto advanceOf = [&](uint32_t c) { return font.Resolve(c).advance; };
    auto finish = [&](size_t begin, size_t end) {
        int width = 0;
        for (size_t i = begin; i < end; ++i)
            width += advanceOf(layout.chars[i]);
        layout.lines.push_back({begin, end, width});
    };

    size_t lineBegin = 0;
    int width = 0;
    long long lastSpace = -1;        // 현재 줄 안의 마지막 공백 위치
    bool afterNewline = false;       // 마지막 줄이 '\n' 바로 뒤라서 비어 있어도 줄로 센다(공백이 줄바꿈에 먹혀 비는 경우는 세지 않는다)
    const size_t n = layout.chars.size();
    for (size_t i = 0; i < n; ++i) {
        const uint32_t c = layout.chars[i];
        if (c == '\n') {
            finish(lineBegin, i);
            lineBegin = i + 1;
            width = 0;
            lastSpace = -1;
            afterNewline = true;
            continue;
        }
        const int adv = advanceOf(c);
        bool swallowed = false;
        // 폭을 넘으면 줄을 바꾼다. 넘긴 단어가 새 줄에도 안 들어가면(공백 없는 긴 낱말) 글자 경계에서 다시 바꾼다.
        while (maxWidth > 0 && i > lineBegin && width + adv > maxWidth) {
            afterNewline = false;
            if (c == ' ') {
                // 줄 끝의 공백은 사라진다: 이 줄을 여기서 닫고 공백은 건너뛴다.
                finish(lineBegin, i);
                lineBegin = i + 1;
                width = 0;
                lastSpace = -1;
                swallowed = true;
                break;
            }
            if (lastSpace >= static_cast<long long>(lineBegin)) {
                finish(lineBegin, static_cast<size_t>(lastSpace));
                lineBegin = static_cast<size_t>(lastSpace) + 1;
            } else {
                finish(lineBegin, i);
                lineBegin = i;
            }
            width = 0;
            lastSpace = -1;
            for (size_t k = lineBegin; k < i; ++k) {
                width += advanceOf(layout.chars[k]);
                if (layout.chars[k] == ' ')
                    lastSpace = static_cast<long long>(k);
            }
        }
        if (swallowed)
            continue;
        if (c == ' ')
            lastSpace = static_cast<long long>(i);
        width += adv;
    }
    if (lineBegin < n || afterNewline)
        finish(lineBegin, n);

    for (const TextLine& l : layout.lines)
        layout.width = std::max(layout.width, l.width);
    layout.height = static_cast<int>(layout.lines.size()) * font.LineHeight();
    return layout;
}

namespace {

inline void PutPixel(uint8_t* row, int x, uint32_t rgb)
{
    uint8_t* p = row + static_cast<size_t>(x) * 4;
    p[0] = static_cast<uint8_t>(rgb & 0xFF);          // B
    p[1] = static_cast<uint8_t>((rgb >> 8) & 0xFF);   // G
    p[2] = static_cast<uint8_t>((rgb >> 16) & 0xFF);  // R
    p[3] = 255;
}

}  // namespace

void DrawLayout(const Font& font, const TextLayout& layout, uint8_t* bgra, int width, int height, int stride, int x, int y, uint32_t rgb)
{
    if (!bgra || width <= 0 || height <= 0)
        return;
    const long long lineHeight = font.LineHeight();
    for (size_t li = 0; li < layout.lines.size(); ++li) {
        const TextLine& line = layout.lines[li];
        const long long baseline = static_cast<long long>(y) + static_cast<long long>(li) * lineHeight + font.Ascent();
        long long pen = x;
        for (size_t i = line.begin; i < line.end; ++i) {
            const Glyph& g = font.Resolve(layout.chars[i]);
            const long long left = pen + g.xOffset;
            const long long top = baseline - g.yOffset - g.height;
            pen += g.advance;
            if (g.width == 0 || g.height == 0 || left >= width || left + g.width <= 0 || top >= height || top + g.height <= 0)
                continue;
            for (int gy = 0; gy < g.height; ++gy) {
                const long long py = top + gy;
                if (py < 0 || py >= height)
                    continue;
                uint8_t* row = bgra + static_cast<size_t>(py) * static_cast<size_t>(stride);
                for (int gx = 0; gx < g.width; ++gx) {
                    const long long px = left + gx;
                    if (px >= 0 && px < width && font.Pixel(g, gx, gy))
                        PutPixel(row, static_cast<int>(px), rgb);
                }
            }
        }
    }
}

void FillRect(uint8_t* bgra, int width, int height, int stride, int x, int y, int rectWidth, int rectHeight, uint32_t rgb, int alpha)
{
    if (!bgra || width <= 0 || height <= 0 || rectWidth <= 0 || rectHeight <= 0 || alpha <= 0)
        return;
    if (alpha > 255)
        alpha = 255;
    const long long x0 = std::max<long long>(x, 0), y0 = std::max<long long>(y, 0);
    const long long x1 = std::min<long long>(static_cast<long long>(x) + rectWidth, width), y1 = std::min<long long>(static_cast<long long>(y) + rectHeight, height);
    if (x0 >= x1 || y0 >= y1)
        return;
    const int sb = static_cast<int>(rgb & 0xFF), sg = static_cast<int>((rgb >> 8) & 0xFF), sr = static_cast<int>((rgb >> 16) & 0xFF);
    for (long long py = y0; py < y1; ++py) {
        uint8_t* row = bgra + static_cast<size_t>(py) * static_cast<size_t>(stride);
        for (long long px = x0; px < x1; ++px) {
            if (alpha == 255) {
                PutPixel(row, static_cast<int>(px), rgb);
                continue;
            }
            uint8_t* p = row + static_cast<size_t>(px) * 4;
            p[0] = static_cast<uint8_t>((sb * alpha + p[0] * (255 - alpha) + 127) / 255);
            p[1] = static_cast<uint8_t>((sg * alpha + p[1] * (255 - alpha) + 127) / 255);
            p[2] = static_cast<uint8_t>((sr * alpha + p[2] * (255 - alpha) + 127) / 255);
        }
    }
}

}  // namespace gge
