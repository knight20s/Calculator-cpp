#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <string>

namespace
{
    constexpr wchar_t kClear = L'C';
    constexpr wchar_t kBack = L'B';
    constexpr wchar_t kEquals = L'=';
    constexpr wchar_t kGroupSeparator = L' ';
    constexpr int kCols = 4;
    constexpr int kRows = 5;
    constexpr int kKeyCount = kCols * kRows;
    constexpr size_t kMaxLength = 200;
    constexpr int kIconId = 1;
    constexpr float kOuterPad = 4.0f;
    constexpr float kGap = 2.0f;
    constexpr float kCornerRadius = 4.0f;
    constexpr float kTextInset = 14.0f;
    constexpr UINT_PTR kAnimationTimer = 1;
    constexpr float kHoverTau = 70.0f;
    constexpr float kPressTau = 120.0f;
    constexpr DWORD kDwmDarkMode = 20;
    constexpr DWORD kDwmBorderColor = 34;
    constexpr DWORD kDwmCaptionColor = 35;
    constexpr DWORD kDwmTextColor = 36;

    enum class Kind
    {
        Digit,
        Function,
        Operator,
        Equals
    };

    struct Palette
    {
        float fill;
        float hover;
        float press;
        float text;
    };

    constexpr Palette kPalettes[] = {
        {30.0f, 46.0f, 84.0f, 255.0f},
        {17.0f, 33.0f, 70.0f, 170.0f},
        {52.0f, 67.0f, 100.0f, 255.0f},
        {255.0f, 222.0f, 176.0f, 0.0f},
    };

    struct Key
    {
        const wchar_t *label;
        const wchar_t *glyph;
        wchar_t code;
        Kind kind;
    };

    constexpr Key kKeys[kKeyCount] = {
        {L"C", nullptr, kClear, Kind::Function},
        {L"(", nullptr, L'(', Kind::Function},
        {L")", nullptr, L')', Kind::Function},
        {L"÷", L"", L'/', Kind::Operator},
        {L"7", nullptr, L'7', Kind::Digit},
        {L"8", nullptr, L'8', Kind::Digit},
        {L"9", nullptr, L'9', Kind::Digit},
        {L"×", L"", L'*', Kind::Operator},
        {L"4", nullptr, L'4', Kind::Digit},
        {L"5", nullptr, L'5', Kind::Digit},
        {L"6", nullptr, L'6', Kind::Digit},
        {L"−", L"", L'-', Kind::Operator},
        {L"1", nullptr, L'1', Kind::Digit},
        {L"2", nullptr, L'2', Kind::Digit},
        {L"3", nullptr, L'3', Kind::Digit},
        {L"+", L"", L'+', Kind::Operator},
        {L"⌫", L"", kBack, Kind::Function},
        {L"0", nullptr, L'0', Kind::Digit},
        {L".", nullptr, L'.', Kind::Digit},
        {L"=", L"", kEquals, Kind::Equals},
    };

    enum class Status
    {
        Ok,
        Syntax,
        DivideByZero,
        Overflow
    };

    struct KeyAnimation
    {
        float hover = 0.0f;
        float press = 0.0f;
    };

    struct App
    {
        HWND hwnd = nullptr;
        UINT dpi = 96;
        std::wstring expression;
        std::wstring history;
        std::wstring result;
        bool showingResult = false;
        bool resultIsError = false;
        int hover = -1;
        int pressed = -1;
        bool trackingMouse = false;
        bool animating = false;
        ULONGLONG lastTick = 0;
        KeyAnimation animation[kKeyCount];
        D2D1_RECT_F keys[kKeyCount] = {};
        D2D1_RECT_F historyRect = {};
        D2D1_RECT_F mainRect = {};
        float textOffset = 0.0f;
        ID2D1Factory *d2d = nullptr;
        IDWriteFactory *dwrite = nullptr;
        ID2D1HwndRenderTarget *target = nullptr;
        ID2D1SolidColorBrush *brush = nullptr;
        IDWriteTextFormat *digitFormat = nullptr;
        IDWriteTextFormat *labelFormat = nullptr;
        IDWriteTextFormat *iconFormat = nullptr;
        IDWriteTextFormat *historyFormat = nullptr;
        IDWriteTextFormat *mainFormat = nullptr;
        std::wstring textFamily = L"Segoe UI";
        std::wstring displayFamily = L"Segoe UI";
        std::wstring iconFamily;
    };

    App app;

    template <typename T>
    void release(T *&object)
    {
        if (object)
        {
            object->Release();
            object = nullptr;
        }
    }

    bool isDigit(wchar_t c)
    {
        return c >= L'0' && c <= L'9';
    }

    bool isOperator(wchar_t c)
    {
        return c == L'+' || c == L'-' || c == L'*' || c == L'/';
    }

    class Evaluator
    {
    public:
        explicit Evaluator(const std::wstring &text) : pos_(text.c_str()), end_(text.c_str() + text.size()) {}

        Status run(double &value)
        {
            if (!expression(value))
                return status_;
            if (pos_ != end_)
                return Status::Syntax;
            if (!std::isfinite(value))
                return Status::Overflow;
            return Status::Ok;
        }

    private:
        const wchar_t *pos_;
        const wchar_t *end_;
        Status status_ = Status::Syntax;

        bool peek(wchar_t c) const
        {
            return pos_ < end_ && *pos_ == c;
        }

        bool expression(double &value)
        {
            if (!term(value))
                return false;
            while (peek(L'+') || peek(L'-'))
            {
                const wchar_t op = *pos_++;
                double rhs = 0.0;
                if (!term(rhs))
                    return false;
                value = op == L'+' ? value + rhs : value - rhs;
            }
            return true;
        }

        bool term(double &value)
        {
            if (!factor(value))
                return false;
            while (peek(L'*') || peek(L'/'))
            {
                const wchar_t op = *pos_++;
                double rhs = 0.0;
                if (!factor(rhs))
                    return false;
                if (op == L'*')
                {
                    value *= rhs;
                }
                else if (rhs == 0.0)
                {
                    status_ = Status::DivideByZero;
                    return false;
                }
                else
                {
                    value /= rhs;
                }
            }
            return true;
        }

        bool factor(double &value)
        {
            if (peek(L'-') || peek(L'+'))
            {
                const bool negate = *pos_++ == L'-';
                if (!factor(value))
                    return false;
                if (negate)
                    value = -value;
                return true;
            }
            if (peek(L'('))
            {
                ++pos_;
                if (!expression(value))
                    return false;
                if (!peek(L')'))
                    return false;
                ++pos_;
                return true;
            }
            if (pos_ < end_ && (isDigit(*pos_) || *pos_ == L'.'))
            {
                wchar_t *stop = nullptr;
                value = std::wcstod(pos_, &stop);
                if (stop == pos_)
                    return false;
                pos_ = stop;
                return true;
            }
            return false;
        }
    };

    std::wstring formatNumber(double value)
    {
        value += 0.0;
        char buffer[32];
        const auto converted = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::general, 15);
        return std::wstring(buffer, converted.ptr);
    }

    const wchar_t *statusText(Status status)
    {
        switch (status)
        {
        case Status::DivideByZero:
            return L"Деление на ноль";
        case Status::Overflow:
            return L"Слишком большое число";
        default:
            return L"Ошибка";
        }
    }

    void appendGrouped(std::wstring &out, const std::wstring &text, size_t begin, size_t end)
    {
        size_t integerEnd = begin;
        while (integerEnd < end && text[integerEnd] != L'.')
            ++integerEnd;
        for (size_t i = begin; i < integerEnd; ++i)
        {
            if (i > begin && (integerEnd - i) % 3 == 0)
                out += kGroupSeparator;
            out += text[i];
        }
        out.append(text, integerEnd, end - integerEnd);
    }

    std::wstring prettify(const std::wstring &text)
    {
        std::wstring out;
        out.reserve(text.size() * 3);
        wchar_t prev = 0;
        size_t i = 0;
        while (i < text.size())
        {
            const wchar_t c = text[i];
            if (isDigit(c) || c == L'.')
            {
                size_t end = i;
                while (end < text.size() && (isDigit(text[end]) || text[end] == L'.'))
                    ++end;
                appendGrouped(out, text, i, end);
                prev = text[end - 1];
                i = end;
                continue;
            }
            const bool exponentSign = prev == L'e';
            switch (c)
            {
            case L'*':
                out += L" × ";
                break;
            case L'/':
                out += L" ÷ ";
                break;
            case L'+':
                out += exponentSign ? L"+" : L" + ";
                break;
            case L'-':
                if (exponentSign)
                    out += L'-';
                else if (prev == 0 || prev == L'(' || isOperator(prev))
                    out += L'−';
                else
                    out += L" − ";
                break;
            default:
                out += c;
            }
            prev = c;
            ++i;
        }
        while (!out.empty() && out.back() == L' ')
            out.pop_back();
        return out;
    }

    size_t numberStart(const std::wstring &text)
    {
        size_t i = text.size();
        while (i > 0 && (isDigit(text[i - 1]) || text[i - 1] == L'.'))
            --i;
        return i;
    }

    int openParens(const std::wstring &text)
    {
        int depth = 0;
        for (const wchar_t c : text)
            depth += (c == L'(') - (c == L')');
        return depth;
    }

    wchar_t normalize(wchar_t c)
    {
        switch (c)
        {
        case L',':
            return L'.';
        case L'x':
        case L'X':
        case L'×':
        case L'х':
        case L'Х':
            return L'*';
        case L':':
        case L'÷':
            return L'/';
        case L'−':
        case L'–':
            return L'-';
        default:
            break;
        }
        if (isDigit(c) || isOperator(c) || c == L'.' || c == L'(' || c == L')')
            return c;
        return 0;
    }

    void inputSymbol(wchar_t c)
    {
        if (app.showingResult)
        {
            const bool chain = isOperator(c) && !app.resultIsError;
            app.expression = chain ? app.result : std::wstring();
            app.showingResult = false;
            app.resultIsError = false;
        }
        std::wstring &e = app.expression;
        if (e.size() + 3 > kMaxLength)
            return;
        const wchar_t last = e.empty() ? 0 : e.back();
        if (isDigit(c))
        {
            if (last == L')')
                e += L'*';
            else if (last == L'0' && numberStart(e) == e.size() - 1)
                e.pop_back();
            e += c;
        }
        else if (c == L'.')
        {
            if (e.find(L'.', numberStart(e)) != std::wstring::npos)
                return;
            if (last == L')')
                e += L'*';
            if (numberStart(e) == e.size())
                e += L'0';
            e += L'.';
        }
        else if (c == L'-')
        {
            if (last == L'+' || last == L'-')
                e.back() = L'-';
            else
                e += L'-';
        }
        else if (isOperator(c))
        {
            size_t n = e.size();
            while (n > 0 && isOperator(e[n - 1]))
                --n;
            if (n == 0 || e[n - 1] == L'(')
                return;
            e.resize(n);
            e += c;
        }
        else if (c == L'(')
        {
            if (isDigit(last) || last == L'.' || last == L')')
                e += L'*';
            e += L'(';
        }
        else if (c == L')')
        {
            if (openParens(e) <= 0 || last == L'(' || isOperator(last))
                return;
            e += L')';
        }
    }

    void erase()
    {
        if (app.showingResult)
        {
            app.showingResult = false;
            app.resultIsError = false;
            app.history.clear();
            return;
        }
        if (!app.expression.empty())
            app.expression.pop_back();
    }

    void clearAll()
    {
        app.expression.clear();
        app.history.clear();
        app.result.clear();
        app.showingResult = false;
        app.resultIsError = false;
    }

    void calculate()
    {
        if (app.showingResult || app.expression.empty())
            return;
        app.expression.append(static_cast<size_t>(std::max(0, openParens(app.expression))), L')');
        double value = 0.0;
        const Status status = Evaluator(app.expression).run(value);
        app.history = prettify(app.expression) + L" =";
        app.resultIsError = status != Status::Ok;
        app.result = app.resultIsError ? statusText(status) : formatNumber(value);
        app.showingResult = true;
    }

    void copyToClipboard()
    {
        if (app.showingResult && app.resultIsError)
            return;
        const std::wstring &text = app.showingResult ? app.result : app.expression;
        if (text.empty() || !OpenClipboard(app.hwnd))
            return;
        EmptyClipboard();
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            if (void *data = GlobalLock(memory))
            {
                std::memcpy(data, text.c_str(), bytes);
                GlobalUnlock(memory);
                if (!SetClipboardData(CF_UNICODETEXT, memory))
                    GlobalFree(memory);
            }
            else
            {
                GlobalFree(memory);
            }
        }
        CloseClipboard();
    }

    void pasteFromClipboard()
    {
        if (!OpenClipboard(app.hwnd))
            return;
        if (HANDLE memory = GetClipboardData(CF_UNICODETEXT))
        {
            if (const auto *text = static_cast<const wchar_t *>(GlobalLock(memory)))
            {
                for (const wchar_t *p = text; *p && app.expression.size() < kMaxLength; ++p)
                {
                    if (const wchar_t c = normalize(*p))
                        inputSymbol(c);
                }
                GlobalUnlock(memory);
            }
        }
        CloseClipboard();
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }

    void startAnimation()
    {
        if (!app.animating)
        {
            app.animating = true;
            app.lastTick = GetTickCount64();
            SetTimer(app.hwnd, kAnimationTimer, 10, nullptr);
        }
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }

    bool approach(float &value, float target, float factor)
    {
        value += (target - value) * factor;
        if (std::fabs(target - value) > 0.003f)
            return true;
        value = target;
        return false;
    }

    void stepAnimation()
    {
        const ULONGLONG now = GetTickCount64();
        const float elapsed = static_cast<float>(now - app.lastTick);
        app.lastTick = now;
        const float hoverFactor = 1.0f - std::exp(-elapsed / kHoverTau);
        const float pressFactor = 1.0f - std::exp(-elapsed / kPressTau);
        bool active = false;
        for (int i = 0; i < kKeyCount; ++i)
        {
            KeyAnimation &state = app.animation[i];
            if (approach(state.hover, i == app.hover ? 1.0f : 0.0f, hoverFactor))
                active = true;
            if (approach(state.press, i == app.pressed ? 1.0f : 0.0f, pressFactor))
                active = true;
        }
        if (!active)
        {
            KillTimer(app.hwnd, kAnimationTimer);
            app.animating = false;
        }
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }

    void press(wchar_t code)
    {
        if (code == kClear)
            clearAll();
        else if (code == kBack)
            erase();
        else if (code == kEquals)
            calculate();
        else
            inputSymbol(code);
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }

    int keyIndex(wchar_t code)
    {
        for (int i = 0; i < kKeyCount; ++i)
        {
            if (kKeys[i].code == code)
                return i;
        }
        return -1;
    }

    void pressFromKeyboard(wchar_t code)
    {
        const int index = keyIndex(code);
        if (index >= 0)
        {
            app.animation[index].press = 1.0f;
            startAnimation();
        }
        press(code);
    }

    int hitTest(LPARAM lParam)
    {
        const float scale = 96.0f / static_cast<float>(app.dpi);
        const float x = static_cast<float>(GET_X_LPARAM(lParam)) * scale;
        const float y = static_cast<float>(GET_Y_LPARAM(lParam)) * scale;
        for (int i = 0; i < kKeyCount; ++i)
        {
            const D2D1_RECT_F &key = app.keys[i];
            if (x >= key.left && x < key.right && y >= key.top && y < key.bottom)
                return i;
        }
        return -1;
    }

    void setHover(int index)
    {
        if (index == app.hover)
            return;
        app.hover = index;
        startAnimation();
    }

    std::wstring pickFamily(const wchar_t *preferred, const wchar_t *fallback)
    {
        IDWriteFontCollection *fonts = nullptr;
        if (FAILED(app.dwrite->GetSystemFontCollection(&fonts, FALSE)))
            return fallback;
        UINT32 index = 0;
        BOOL exists = FALSE;
        const bool found = SUCCEEDED(fonts->FindFamilyName(preferred, &index, &exists)) && exists;
        fonts->Release();
        return found ? preferred : fallback;
    }

    void chooseFonts()
    {
        app.textFamily = pickFamily(L"Segoe UI Variable Text", L"Segoe UI");
        app.displayFamily = pickFamily(L"Segoe UI Variable Display", L"Segoe UI");
        app.iconFamily = pickFamily(L"Segoe Fluent Icons", L"");
        if (app.iconFamily.empty())
            app.iconFamily = pickFamily(L"Segoe MDL2 Assets", L"");
    }

    IDWriteTextFormat *makeFormat(const std::wstring &family, float size, DWRITE_FONT_WEIGHT weight, bool centered)
    {
        IDWriteTextFormat *format = nullptr;
        if (FAILED(app.dwrite->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL, std::max(1.0f, size), L"ru-ru", &format)))
        {
            return nullptr;
        }
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (centered)
        {
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        return format;
    }

    void replaceFormat(IDWriteTextFormat *&slot, IDWriteTextFormat *format)
    {
        release(slot);
        slot = format;
    }

    void layout()
    {
        RECT client;
        GetClientRect(app.hwnd, &client);
        const float scale = 96.0f / static_cast<float>(app.dpi);
        const float width = static_cast<float>(client.right) * scale;
        const float height = static_cast<float>(client.bottom) * scale;
        const float displayBottom = height * 0.3f;
        const float historyBottom = kOuterPad + (displayBottom - kOuterPad) * 0.32f;
        app.historyRect = {kTextInset, kOuterPad, width - kTextInset, historyBottom};
        app.mainRect = {kTextInset, historyBottom, width - kTextInset, displayBottom};

        const float top = displayBottom + kOuterPad;
        const float keyWidth = std::max(1.0f, (width - 2.0f * kOuterPad - (kCols - 1) * kGap) / kCols);
        const float keyHeight = std::max(1.0f, (height - top - kOuterPad - (kRows - 1) * kGap) / kRows);
        for (int r = 0; r < kRows; ++r)
        {
            for (int c = 0; c < kCols; ++c)
            {
                const float left = kOuterPad + static_cast<float>(c) * (keyWidth + kGap);
                const float keyTop = top + static_cast<float>(r) * (keyHeight + kGap);
                app.keys[r * kCols + c] = {left, keyTop, left + keyWidth, keyTop + keyHeight};
            }
        }

        const float keyText = std::max(12.0f, std::min(keyHeight * 0.3f, keyWidth * 0.22f));
        app.textOffset = keyText * 0.05f;
        replaceFormat(app.digitFormat, makeFormat(app.textFamily, keyText, DWRITE_FONT_WEIGHT_SEMI_BOLD, true));
        replaceFormat(app.labelFormat, makeFormat(app.textFamily, keyText, DWRITE_FONT_WEIGHT_NORMAL, true));
        replaceFormat(app.iconFormat,
                      app.iconFamily.empty() ? nullptr
                                             : makeFormat(app.iconFamily, keyText, DWRITE_FONT_WEIGHT_NORMAL, true));
        replaceFormat(app.historyFormat,
                      makeFormat(app.textFamily, (historyBottom - kOuterPad) * 0.36f, DWRITE_FONT_WEIGHT_NORMAL, false));
        replaceFormat(app.mainFormat, makeFormat(app.displayFamily, (displayBottom - historyBottom) * 0.56f,
                                                 DWRITE_FONT_WEIGHT_SEMI_BOLD, false));
    }

    D2D1_COLOR_F gray(float value)
    {
        D2D1_COLOR_F color;
        color.r = color.g = color.b = value / 255.0f;
        color.a = 1.0f;
        return color;
    }

    D2D1_MATRIX_3X2_F scaleAround(float scale, float x, float y)
    {
        D2D1_MATRIX_3X2_F matrix{};
        matrix._11 = scale;
        matrix._22 = scale;
        matrix._31 = x * (1.0f - scale);
        matrix._32 = y * (1.0f - scale);
        return matrix;
    }

    void setColor(float tone)
    {
        const D2D1_COLOR_F color = gray(tone);
        app.brush->SetColor(&color);
    }

    bool ensureTarget()
    {
        if (app.target)
            return true;
        RECT client;
        GetClientRect(app.hwnd, &client);
        D2D1_RENDER_TARGET_PROPERTIES properties{};
        properties.dpiX = static_cast<float>(app.dpi);
        properties.dpiY = static_cast<float>(app.dpi);
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProperties{};
        hwndProperties.hwnd = app.hwnd;
        hwndProperties.pixelSize.width = static_cast<UINT32>(std::max<LONG>(1, client.right));
        hwndProperties.pixelSize.height = static_cast<UINT32>(std::max<LONG>(1, client.bottom));
        hwndProperties.presentOptions = D2D1_PRESENT_OPTIONS_NONE;
        if (FAILED(app.d2d->CreateHwndRenderTarget(&properties, &hwndProperties, &app.target)))
            return false;
        app.target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        const D2D1_COLOR_F white = gray(255.0f);
        if (FAILED(app.target->CreateSolidColorBrush(&white, nullptr, &app.brush)))
        {
            release(app.target);
            return false;
        }
        return true;
    }

    void releaseTarget()
    {
        release(app.brush);
        release(app.target);
    }

    void drawRightAligned(const std::wstring &text, IDWriteTextFormat *format, const D2D1_RECT_F &rect, float minScale)
    {
        if (text.empty() || !format)
            return;
        IDWriteTextLayout *textLayout = nullptr;
        if (FAILED(app.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, 100000.0f,
                                                rect.bottom - rect.top, &textLayout)))
        {
            return;
        }
        DWRITE_TEXT_METRICS metrics{};
        textLayout->GetMetrics(&metrics);
        const float available = rect.right - rect.left;
        const float scale = metrics.width > available ? std::max(minScale, available / metrics.width) : 1.0f;
        const D2D1_MATRIX_3X2_F transform = scaleAround(scale, rect.right, rect.bottom);
        const D2D1_MATRIX_3X2_F identity = scaleAround(1.0f, 0.0f, 0.0f);
        app.target->PushAxisAlignedClip(&rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        app.target->SetTransform(&transform);
        app.target->DrawTextLayout(D2D1_POINT_2F{rect.right - metrics.width, rect.bottom - metrics.height}, textLayout,
                                   app.brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
        app.target->SetTransform(&identity);
        app.target->PopAxisAlignedClip();
        textLayout->Release();
    }

    void drawKey(int index)
    {
        const Key &key = kKeys[index];
        const Palette &palette = kPalettes[static_cast<int>(key.kind)];
        const KeyAnimation &state = app.animation[index];
        float tone = palette.fill + (palette.hover - palette.fill) * state.hover;
        tone += (palette.press - tone) * state.press;

        const D2D1_ROUNDED_RECT shape{app.keys[index], kCornerRadius, kCornerRadius};
        setColor(tone);
        app.target->FillRoundedRectangle(&shape, app.brush);

        const bool icon = key.glyph && app.iconFormat;
        IDWriteTextFormat *format = icon ? app.iconFormat : key.kind == Kind::Digit ? app.digitFormat
                                                                                    : app.labelFormat;
        if (!format)
            return;
        const wchar_t *label = icon ? key.glyph : key.label;
        D2D1_RECT_F textRect = app.keys[index];
        if (!icon)
        {
            textRect.top -= app.textOffset;
            textRect.bottom -= app.textOffset;
        }
        setColor(palette.text);
        app.target->DrawText(label, static_cast<UINT32>(std::wcslen(label)), format, &textRect, app.brush,
                             D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    }

    void render()
    {
        if (!ensureTarget())
            return;
        ID2D1HwndRenderTarget *target = app.target;
        target->BeginDraw();
        const D2D1_MATRIX_3X2_F identity = scaleAround(1.0f, 0.0f, 0.0f);
        target->SetTransform(&identity);
        const D2D1_COLOR_F background = gray(0.0f);
        target->Clear(&background);

        setColor(120.0f);
        drawRightAligned(app.history, app.historyFormat, app.historyRect, 0.6f);

        std::wstring main = app.resultIsError ? app.result : prettify(app.showingResult ? app.result : app.expression);
        if (main.empty())
            main = L"0";
        setColor(255.0f);
        drawRightAligned(main, app.mainFormat, app.mainRect, 0.4f);

        for (int i = 0; i < kKeyCount; ++i)
            drawKey(i);

        if (target->EndDraw(nullptr, nullptr) == static_cast<HRESULT>(D2DERR_RECREATE_TARGET))
            releaseTarget();
    }

    void applyDarkFrame(HWND hwnd)
    {
        const BOOL dark = TRUE;
        const COLORREF caption = RGB(0, 0, 0);
        const COLORREF captionText = RGB(255, 255, 255);
        const COLORREF border = RGB(38, 38, 38);
        DwmSetWindowAttribute(hwnd, kDwmDarkMode, &dark, sizeof dark);
        DwmSetWindowAttribute(hwnd, kDwmCaptionColor, &caption, sizeof caption);
        DwmSetWindowAttribute(hwnd, kDwmTextColor, &captionText, sizeof captionText);
        DwmSetWindowAttribute(hwnd, kDwmBorderColor, &border, sizeof border);
    }

    LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_NCCREATE:
            app.hwnd = hwnd;
            break;
        case WM_CREATE:
            app.dpi = GetDpiForWindow(hwnd);
            return 0;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED)
            {
                if (app.target)
                {
                    const D2D1_SIZE_U size{LOWORD(lParam), HIWORD(lParam)};
                    app.target->Resize(&size);
                }
                layout();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_GETMINMAXINFO:
        {
            const float scale = static_cast<float>(app.dpi) / 96.0f;
            auto *info = reinterpret_cast<MINMAXINFO *>(lParam);
            info->ptMinTrackSize = {static_cast<LONG>(260 * scale), static_cast<LONG>(400 * scale)};
            return 0;
        }
        case WM_DPICHANGED:
        {
            app.dpi = HIWORD(wParam);
            if (app.target)
                app.target->SetDpi(static_cast<float>(app.dpi), static_cast<float>(app.dpi));
            const auto *suggested = reinterpret_cast<const RECT *>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_SYSCOMMAND:
        {
            const WPARAM command = wParam & 0xFFF0;
            if (command == SC_KEYMENU || command == SC_MOUSEMENU)
                return 0;
            break;
        }
        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONDBLCLK:
            if (wParam == HTSYSMENU)
                return 0;
            break;
        case WM_CONTEXTMENU:
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            render();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DISPLAYCHANGE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            if (!app.trackingMouse)
            {
                TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, hwnd, 0};
                app.trackingMouse = TrackMouseEvent(&track) != FALSE;
            }
            setHover(hitTest(lParam));
            return 0;
        case WM_MOUSELEAVE:
            app.trackingMouse = false;
            setHover(-1);
            return 0;
        case WM_LBUTTONDOWN:
        {
            const int index = hitTest(lParam);
            if (index >= 0)
            {
                app.pressed = index;
                app.animation[index].press = 1.0f;
                SetCapture(hwnd);
                startAnimation();
                press(kKeys[index].code);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (app.pressed >= 0)
            {
                app.pressed = -1;
                ReleaseCapture();
                startAnimation();
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (app.pressed >= 0)
            {
                app.pressed = -1;
                startAnimation();
            }
            return 0;
        case WM_TIMER:
            if (wParam == kAnimationTimer)
                stepAnimation();
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_DELETE)
            {
                pressFromKeyboard(kClear);
                return 0;
            }
            break;
        case WM_CHAR:
        {
            const wchar_t ch = static_cast<wchar_t>(wParam);
            switch (ch)
            {
            case L'\r':
            case L'=':
                pressFromKeyboard(kEquals);
                break;
            case L'\b':
                pressFromKeyboard(kBack);
                break;
            case 0x1B:
                pressFromKeyboard(kClear);
                break;
            case 0x03:
                copyToClipboard();
                break;
            case 0x16:
                pasteFromClipboard();
                break;
            default:
                if (const wchar_t code = normalize(ch))
                    pressFromKeyboard(code);
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                                 reinterpret_cast<void **>(&app.d2d))))
    {
        return 1;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(&app.dwrite))))
    {
        return 1;
    }
    chooseFonts();
    app.dpi = GetDpiForSystem();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof windowClass;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = L"MonoCalculator";
    windowClass.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kIconId), IMAGE_ICON,
                                                      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kIconId), IMAGE_ICON,
                                                        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (!RegisterClassExW(&windowClass))
        return 1;

    const float scale = static_cast<float>(app.dpi) / 96.0f;
    RECT frame{0, 0, static_cast<LONG>(340 * scale), static_cast<LONG>(520 * scale)};
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, app.dpi);
    HWND hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"Калькулятор", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr,
                                instance, nullptr);
    if (!hwnd)
        return 1;
    applyDarkFrame(hwnd);
    ShowWindow(hwnd, show);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
