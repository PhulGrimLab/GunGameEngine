#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gge {

// 비트맵 폰트(BDF)와 글자 그리기. 렌더러와 무관한 순수 CPU 로직이며, 그리기는 호출자가 준 BGRA 버퍼에 정수 연산만으로 덮어쓴다
// (그래서 WARP/GPU/에디터/플레이어/헤드리스에서 바이트 단위로 같다).

constexpr size_t kMaxFontFileBytes = 32u << 20;   // BDF 파일 크기 상한(Galmuri11.bdf는 약 2.9MB)
constexpr int kMaxFontGlyphs = 200000;            // CHARS 상한(Galmuri11은 20965)
constexpr int kMaxGlyphSide = 64;                 // 글리프 비트맵 가로/세로 상한(픽셀)
constexpr int kMaxGlyphAdvance = 256;             // 글리프 하나의 펜 이동 상한(픽셀)
constexpr int kMaxFontMetric = 256;               // ascent/descent 상한
constexpr size_t kMaxTextBytes = 1024;            // 한 번에 그리는/재는 UTF-8 문자열 상한
constexpr uint32_t kReplacementChar = 0xFFFD;     // 잘못된 UTF-8 바이트를 나타내는 글자

struct Glyph {
    int advance = 0;                    // 펜 이동(픽셀)
    int width = 0, height = 0;          // 비트맵 크기
    int xOffset = 0, yOffset = 0;       // 기준선/펜 위치에서 비트맵 왼쪽 아래 모서리까지(BDF BBX): 위쪽이 +y
    size_t bitsOffset = 0;              // Font의 비트 저장소에서의 시작 바이트. 행마다 (width+7)/8바이트, 위 행부터, 왼쪽 픽셀이 최상위 비트
};

class Font {
public:
    // BDF 텍스트를 읽는다. 잘못된 입력(잘린 파일, 글리프 수 불일치, 한도 초과, 비트맵 줄 수/길이 불일치, 중복 인코딩, 비정상 문자 등)은
    // "line N: 사유"가 담긴 std::runtime_error로 거부하며 메모리는 입력 크기에 비례하는 만큼만 쓴다.
    static Font Parse(std::string_view bdfText);
    static Font LoadFile(const std::string& pathUtf8);

    int Ascent() const { return _ascent; }
    int Descent() const { return _descent; }
    int LineHeight() const { return _ascent + _descent; }
    size_t GlyphCount() const { return _glyphs.size(); }

    // 코드 포인트의 글리프. 없으면 nullptr.
    const Glyph* Find(uint32_t codePoint) const;
    // 그릴 글리프: 있으면 그것, 없으면 폰트의 .notdef(없으면 폭 0의 빈 글리프). 잘못된 UTF-8은 U+FFFD(없으면 .notdef)로 바뀌어 이 함수에 온다.
    const Glyph& Resolve(uint32_t codePoint) const;
    bool Pixel(const Glyph& glyph, int x, int y) const;   // 범위 밖이면 false

private:
    int _ascent = 0, _descent = 0;
    std::vector<Glyph> _glyphs;
    std::unordered_map<uint32_t, uint32_t> _index;   // 코드 포인트 -> _glyphs 번호
    std::vector<uint8_t> _bits;
    Glyph _empty;
    int _notdef = -1;                                 // .notdef 글리프 번호(없으면 -1)
};

// UTF-8 해독(WHATWG 규칙): 잘못된 바이트열(잘림, overlong, 서로게이트, U+10FFFF 초과, 단독 연속 바이트)은 "최대 부분 열" 하나당 U+FFFD 하나로 바꾼다.
std::vector<uint32_t> DecodeUtf8(std::string_view text);

struct TextLine {
    size_t begin = 0, end = 0;   // TextLayout::chars의 구간 [begin, end)
    int width = 0;
};

struct TextLayout {
    std::vector<uint32_t> chars;   // 그릴 코드 포인트(제어 문자는 빠짐)
    std::vector<TextLine> lines;
    int width = 0, height = 0;     // 가장 넓은 줄의 폭, 줄 수 x 줄 높이
};

// 글자를 줄로 나눈다. '\n'은 줄바꿈(N개면 N+1줄: 끝의 '\n'은 빈 마지막 줄을 만든다), 탭/CR/그 밖의 제어 문자(U+0000..001F, U+007F..009F)는 무시한다.
// maxWidth > 0이면 그 픽셀 폭을 넘기 전에 줄을 바꾼다: 그 줄 안의 마지막 공백에서(공백은 줄 끝에서 사라짐), 공백이 없으면 글자 경계에서.
// 폭보다 넓은 글자 하나는 자기 줄에 혼자 놓인다(무한 루프 없음). maxWidth <= 0이면 줄바꿈은 '\n'뿐. 빈 문자열은 0줄. 문자열이 kMaxTextBytes를 넘으면 예외.
TextLayout LayoutText(const Font& font, std::string_view utf8, int maxWidth);

// BGRA 버퍼(행 간격 stride 바이트)에 글자를 그린다. (x, y)는 첫 줄 윗변의 왼쪽 위(줄 높이 = ascent + descent, 기준선 = 윗변 + ascent).
// 글자는 1비트라 색을 그대로 덮어쓰고(불투명), 버퍼 밖과 음수 좌표는 잘라서 그린다. rgb는 0xRRGGBB.
void DrawLayout(const Font& font, const TextLayout& layout, uint8_t* bgra, int width, int height, int stride, int x, int y, uint32_t rgb);

// 채운 사각형. alpha 0..255(255 = 불투명 덮어쓰기, 0 = 그리지 않음): 정수 연산 (src*a + dst*(255-a) + 127) / 255. 버퍼 밖은 잘라서 그린다.
void FillRect(uint8_t* bgra, int width, int height, int stride, int x, int y, int rectWidth, int rectHeight, uint32_t rgb, int alpha);

}  // namespace gge
