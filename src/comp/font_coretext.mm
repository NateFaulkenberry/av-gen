// CoreText font backend (ADR-083). macOS only; font_stub.cpp stands in elsewhere.
//
// Two deliberate choices here:
//
// * Glyphs are rasterised from their **outline** (CTFontCreatePathForGlyph filled by CoreGraphics),
//   not by CTFontDrawGlyphs. Drawing a glyph goes through hinting, font smoothing and subpixel
//   positioning -- three things that are tuned for a screen, vary with system settings, and would
//   make the atlas a function of the machine's appearance preferences. Filling the outline is pure
//   geometry and gives the same bytes on every Mac.
// * Nothing is ever substituted silently. resolve() reports what it found; the caller logs it.

#include "comp/font.hpp"

#include "core/log.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace avgen::comp {

namespace {

// Shaping is done at this size and divided down, rather than at 1.0: CoreText's advances are
// computed in the size it is handed, and a size of 1 puts every advance inside the last few bits
// of a double for no benefit.
constexpr double kShapeSize = 1000.0;

struct CFDeleter {
    void operator()(CFTypeRef ref) const {
        if (ref != nullptr) {
            CFRelease(ref);
        }
    }
};

CFStringRef makeString(std::string_view s) {
    return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(s.data()),
                                   static_cast<CFIndex>(s.size()), kCFStringEncodingUTF8, false);
}

std::string toStdString(CFStringRef s) {
    if (s == nullptr) {
        return {};
    }
    const CFIndex length = CFStringGetLength(s);
    const CFIndex max = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(max), '\0');
    if (!CFStringGetCString(s, out.data(), max, kCFStringEncodingUTF8)) {
        return {};
    }
    out.resize(std::char_traits<char>::length(out.c_str()));
    return out;
}

class CoreTextBackend final : public FontBackend {
public:
    ~CoreTextBackend() override {
        for (auto& [key, font] : fonts_) {
            if (font != nullptr) {
                CFRelease(font);
            }
        }
    }

    const std::vector<std::string>& families() override {
        std::lock_guard lock(mutex_);
        if (!families_.empty()) {
            return families_;
        }
        CFArrayRef names = CTFontManagerCopyAvailableFontFamilyNames();
        if (names != nullptr) {
            const CFIndex count = CFArrayGetCount(names);
            families_.reserve(static_cast<std::size_t>(count));
            for (CFIndex i = 0; i < count; ++i) {
                std::string name = toStdString(static_cast<CFStringRef>(CFArrayGetValueAtIndex(names, i)));
                if (!name.empty() && name.front() != '.') {
                    families_.push_back(std::move(name));
                }
            }
            CFRelease(names);
        }
        std::sort(families_.begin(), families_.end());
        return families_;
    }

    FontResolution resolve(const FontDesc& desc) override {
        std::lock_guard lock(mutex_);
        return resolveLocked(desc);
    }

    Result<ShapedText> shape(const FontDesc& desc, std::string_view utf8) override {
        std::lock_guard lock(mutex_);
        const std::string key = desc.key() + "\n" + std::string(utf8);
        if (const auto it = shaped_.find(key); it != shaped_.end()) {
            return it->second;
        }
        CTFontRef font = fontFor(desc, kShapeSize);
        if (font == nullptr) {
            return fail("no font for '{}'", desc.family);
        }
        ShapedText out;
        out.ascent = static_cast<float>(CTFontGetAscent(font) / kShapeSize);
        out.descent = static_cast<float>(CTFontGetDescent(font) / kShapeSize);
        out.lineHeight =
            static_cast<float>((CTFontGetAscent(font) + CTFontGetDescent(font) + CTFontGetLeading(font)) / kShapeSize);
        if (!(out.lineHeight > 0.0f)) {
            out.lineHeight = 1.2f;
        }

        std::uint32_t lineIndex = 0;
        std::size_t start = 0;
        for (;;) {
            const std::size_t breakAt = utf8.find('\n', start);
            const std::string_view lineText =
                utf8.substr(start, breakAt == std::string_view::npos ? std::string_view::npos : breakAt - start);
            float width = 0.0f;
            shapeLine(font, lineText, lineIndex, out, width);
            out.lineWidths.push_back(width);
            ++lineIndex;
            if (breakAt == std::string_view::npos) {
                break;
            }
            start = breakAt + 1;
        }
        // A trailing newline produces a final empty line, which is what an author who pressed
        // return expects to see in the line count.
        auto [it, inserted] = shaped_.emplace(key, std::move(out));
        return it->second;
    }

    Result<GlyphImage> glyph(const FontDesc& desc, std::uint16_t glyphId) override {
        std::lock_guard lock(mutex_);
        const std::string key = desc.key() + "#" + std::to_string(glyphId);
        if (const auto it = glyphs_.find(key); it != glyphs_.end()) {
            return it->second;
        }
        const auto emPixels = static_cast<double>(kSdfEmTexels * kSdfRasterScale);
        CTFontRef font = fontFor(desc, emPixels);
        if (font == nullptr) {
            return fail("no font for '{}'", desc.family);
        }
        GlyphImage image;
        const auto cg = static_cast<CGGlyph>(glyphId);
        CGPathRef path = CTFontCreatePathForGlyph(font, cg, nullptr);
        if (path == nullptr) {
            // A space, or a glyph with no outline. Real, and drawn as nothing.
            auto [it, inserted] = glyphs_.emplace(key, image);
            return it->second;
        }
        const CGRect bounds = CGPathGetPathBoundingBox(path);
        const auto margin = static_cast<double>(kSdfSpreadTexels * static_cast<float>(kSdfRasterScale));
        const double x0 = std::floor(CGRectGetMinX(bounds) - margin);
        const double y0 = std::floor(CGRectGetMinY(bounds) - margin);
        const double x1 = std::ceil(CGRectGetMaxX(bounds) + margin);
        const double y1 = std::ceil(CGRectGetMaxY(bounds) + margin);
        const auto scale = static_cast<std::uint32_t>(kSdfRasterScale);
        // Rounded up to whole atlas texels so the downsample divides exactly.
        auto width = static_cast<std::uint32_t>(std::max(1.0, x1 - x0));
        auto height = static_cast<std::uint32_t>(std::max(1.0, y1 - y0));
        width = ((width + scale - 1) / scale) * scale;
        height = ((height + scale - 1) / scale) * scale;

        std::vector<std::uint8_t> coverage(static_cast<std::size_t>(width) * height, 0);
        CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
        CGContextRef ctx = CGBitmapContextCreate(coverage.data(), width, height, 8, width, gray, kCGImageAlphaNone);
        CGColorSpaceRelease(gray);
        if (ctx == nullptr) {
            CGPathRelease(path);
            return fail("cannot rasterise glyph {} ({}x{})", glyphId, width, height);
        }
        CGContextSetShouldAntialias(ctx, true);
        CGContextSetAllowsAntialiasing(ctx, true);
        CGContextSetGrayFillColor(ctx, 1.0, 1.0);
        CGContextTranslateCTM(ctx, -x0, -y0);
        CGContextAddPath(ctx, path);
        CGContextFillPath(ctx);
        CGContextRelease(ctx);
        CGPathRelease(path);

        image.sdf = buildSdf(coverage.data(), width, height, scale, kSdfSpreadTexels);
        image.originX = static_cast<float>(x0 / emPixels);
        image.originY = static_cast<float>(y0 / emPixels);
        image.sizeX = static_cast<float>(static_cast<double>(width) / emPixels);
        image.sizeY = static_cast<float>(static_cast<double>(height) / emPixels);
        auto [it, inserted] = glyphs_.emplace(key, std::move(image));
        return it->second;
    }

    [[nodiscard]] bool available() const override { return true; }

private:
    void shapeLine(CTFontRef font, std::string_view text, std::uint32_t lineIndex, ShapedText& out, float& width) {
        if (text.empty()) {
            width = 0.0f;
            return;
        }
        CFStringRef cf = makeString(text);
        if (cf == nullptr) {
            return;
        }
        CFStringRef keys[1] = {kCTFontAttributeName};
        CFTypeRef values[1] = {font};
        CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault, reinterpret_cast<const void**>(keys), values, 1,
                                                   &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFAttributedStringRef attributed = CFAttributedStringCreate(kCFAllocatorDefault, cf, attrs);
        CTLineRef line = CTLineCreateWithAttributedString(attributed);
        if (line != nullptr) {
            CGFloat ascent = 0;
            CGFloat descent = 0;
            CGFloat leading = 0;
            width = static_cast<float>(CTLineGetTypographicBounds(line, &ascent, &descent, &leading) / kShapeSize);
            CFArrayRef runs = CTLineGetGlyphRuns(line);
            const CFIndex runCount = runs == nullptr ? 0 : CFArrayGetCount(runs);
            for (CFIndex r = 0; r < runCount; ++r) {
                auto run = static_cast<CTRunRef>(CFArrayGetValueAtIndex(runs, r));
                const CFIndex count = CTRunGetGlyphCount(run);
                if (count <= 0) {
                    continue;
                }
                std::vector<CGGlyph> ids(static_cast<std::size_t>(count));
                std::vector<CGPoint> positions(static_cast<std::size_t>(count));
                CTRunGetGlyphs(run, CFRangeMake(0, count), ids.data());
                CTRunGetPositions(run, CFRangeMake(0, count), positions.data());
                for (CFIndex i = 0; i < count; ++i) {
                    ShapedGlyph g;
                    g.glyphId = static_cast<std::uint16_t>(ids[static_cast<std::size_t>(i)]);
                    g.x = static_cast<float>(positions[static_cast<std::size_t>(i)].x / kShapeSize);
                    g.y = -static_cast<float>(lineIndex) * out.lineHeight;
                    g.line = lineIndex;
                    out.glyphs.push_back(g);
                }
            }
            CFRelease(line);
        }
        CFRelease(attributed);
        CFRelease(attrs);
        CFRelease(cf);
    }

    FontResolution resolveLocked(const FontDesc& desc) {
        FontResolution out;
        CTFontRef font = fontFor(desc, kShapeSize);
        if (font == nullptr) {
            return out;
        }
        out.available = true;
        CFStringRef ps = CTFontCopyPostScriptName(font);
        CFStringRef fam = CTFontCopyFamilyName(font);
        out.postScriptName = toStdString(ps);
        out.family = toStdString(fam);
        if (ps != nullptr) {
            CFRelease(ps);
        }
        if (fam != nullptr) {
            CFRelease(fam);
        }
        const auto casefold = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            std::erase(s, ' ');
            return s;
        };
        if (!desc.postScriptName.empty()) {
            out.substituted = casefold(out.postScriptName) != casefold(desc.postScriptName);
        } else {
            out.substituted = casefold(out.family) != casefold(desc.family);
        }
        return out;
    }

    // A retained CTFont for this description at this size. Sizes are two fixed constants, so the
    // cache never grows with the composition.
    CTFontRef fontFor(const FontDesc& desc, double size) {
        const std::string key = desc.key() + "@" + std::to_string(static_cast<int>(size));
        if (const auto it = fonts_.find(key); it != fonts_.end()) {
            return it->second;
        }
        CTFontRef font = create(desc.postScriptName, desc.family, desc.weight, desc.italic, size);
        if (font == nullptr && !desc.postScriptName.empty()) {
            font = create({}, desc.family, desc.weight, desc.italic, size);
        }
        if (font == nullptr && !desc.fallback.empty()) {
            font = create({}, desc.fallback, desc.weight, desc.italic, size);
        }
        if (font == nullptr) {
            font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, size, nullptr);
        }
        fonts_.emplace(key, font);
        return font;
    }

    static CTFontRef create(const std::string& postScript, const std::string& family, float weight, bool italic,
                            double size) {
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);
        if (!postScript.empty()) {
            CFStringRef name = makeString(postScript);
            CFDictionarySetValue(attrs, kCTFontNameAttribute, name);
            CFRelease(name);
        } else if (!family.empty()) {
            CFStringRef name = makeString(family);
            CFDictionarySetValue(attrs, kCTFontFamilyNameAttribute, name);
            CFRelease(name);
        } else {
            CFRelease(attrs);
            return nullptr;
        }
        if (postScript.empty() && (weight != 0.0f || italic)) {
            CFMutableDictionaryRef traits = CFDictionaryCreateMutable(
                kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            const auto w = static_cast<double>(weight);
            CFNumberRef weightRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &w);
            CFDictionarySetValue(traits, kCTFontWeightTrait, weightRef);
            CFRelease(weightRef);
            if (italic) {
                const double slant = 0.2;
                CFNumberRef slantRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &slant);
                CFDictionarySetValue(traits, kCTFontSlantTrait, slantRef);
                CFRelease(slantRef);
                const int32_t symbolic = kCTFontTraitItalic;
                CFNumberRef symbolicRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &symbolic);
                CFDictionarySetValue(traits, kCTFontSymbolicTrait, symbolicRef);
                CFRelease(symbolicRef);
            }
            CFDictionarySetValue(attrs, kCTFontTraitsAttribute, traits);
            CFRelease(traits);
        }
        CTFontDescriptorRef descriptor = CTFontDescriptorCreateWithAttributes(attrs);
        CFRelease(attrs);
        if (descriptor == nullptr) {
            return nullptr;
        }
        CTFontRef font = CTFontCreateWithFontDescriptor(descriptor, size, nullptr);
        CFRelease(descriptor);
        return font;
    }

    std::mutex mutex_;
    std::vector<std::string> families_;
    std::unordered_map<std::string, CTFontRef> fonts_;
    std::unordered_map<std::string, ShapedText> shaped_;
    std::unordered_map<std::string, GlyphImage> glyphs_;
};

} // namespace

FontBackend& fontBackend() {
    static CoreTextBackend backend;
    return backend;
}

} // namespace avgen::comp
