#include "game/Sprite.h"

#include <Windows.h>

#include <D2Gfx.h>  // D2CellFileStrc, D2GfxCellStrc

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/GameHelpers.h"
#include "imports/D2Cmp.h"
#include "imports/D2Gfx.h"
#include "imports/D2Win.h"
#include "utils/utils.h"

// NOLINTBEGIN(readability-identifier-naming) - game struct fields match binary layout from reference

namespace d2bs::game {

using namespace d2bs::imports;

namespace {

// Scratch context passed to D2GFX_DrawAutomapCell2. The renderer scribbles
// internal state through the rest of the struct; only pCellFile at 0x34 is
// caller-meaningful. Not a game-resident type - purely a 0x48-byte buffer
// the call site owns for the duration of one draw.
struct CellContext {
    std::array<uint32_t, 13> _1;
    D2CellFileStrc* pCellFile;
    std::array<uint32_t, 4> _2;
};
static_assert(offsetof(CellContext, pCellFile) == 0x34, "pCellFile must sit at 0x34");

// Process-lifetime cache. Two maps so filesystem and MPQ paths can
// share string keys without colliding (e.g., a relative MPQ path and a
// short absolute path that happens to match). One mutex covers both.
struct Cache {
    std::unordered_map<std::string, void*> fs;
    std::unordered_map<std::string, void*> mpq;
    std::mutex mu;
};

Cache& GlobalCache() {
    static Cache c;
    return c;
}

std::shared_ptr<spdlog::logger>& Logger() {
    static auto logger = d2bs::utils::GetLogger("sprite");
    return logger;
}

// Reference D2Helpers.cpp:643-672. Convert an 8-bit indexed BMP scanline
// buffer into a synthetic DC6 cell. Allocates with `new BYTE[]`; ownership
// passes to the caller (and ultimately the cache).
//
// Caller is responsible for ensuring `pixels` covers at least
// `((width + 3) & ~3) * height` bytes; LoadFromBmp does the bounds check.
void* WrapBmpAsDc6(const uint8_t* pixels, uint32_t width, uint32_t height) {
    // Stride is 4-byte aligned per BMP convention. All multiplications below
    // are done in size_t to avoid 32-bit overflow on pathological inputs.
    const auto w = static_cast<size_t>(width);
    const auto h = static_cast<size_t>(height);
    const auto stride = (w + 3U) & ~size_t{3U};
    // Worst-case RLE expansion is 2 bytes per pixel + 1 terminator per row.
    std::vector<uint8_t> rle((w * h * 2U) + h);
    uint8_t* dest = rle.data();
    // BMP rows are stored bottom-up. The reference iterates them in that
    // order (top scanline of pixels[] maps to top of the sprite); we keep
    // the same convention to stay binary-compatible with D2's renderer.
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* src = pixels + (static_cast<size_t>(row) * stride);
        const uint8_t* limit = src + width;
        while (src < limit) {
            const uint8_t* start = src;
            const uint8_t* limit2 = std::min(limit, src + 0x7f);
            const uint8_t trans = (*src == 0) ? 1 : 0;
            do {
                ++src;
            } while ((trans == ((*src == 0) ? 1 : 0)) && (src < limit2));
            if (!trans || (src < limit)) {
                *dest++ = static_cast<uint8_t>((trans ? 0x80 : 0) + (src - start));
            }
            if (!trans) {
                while (start < src) {
                    *dest++ = *start++;
                }
            }
        }
        *dest++ = 0x80;
    }
    const size_t rleLen = static_cast<size_t>(dest - rle.data());

    // DC6 termination marker (3 bytes of 0xee at end of pixel data). Matches
    // reference D2Helpers.cpp:662 sentinel.
    constexpr uint32_t DC6_TERMINATION_MARKER = 0xeeeeeeeeU;

    // Synthetic DC6 header - values match reference D2Helpers.cpp:662.
    std::array<uint32_t, 15> head = {
        6, 1, 0, DC6_TERMINATION_MARKER, 1, 1, 0x1c, 0, width, height, 0, 0, 0, 0, 0,
    };
    head[14] = static_cast<uint32_t>(rleLen);
    head[13] = static_cast<uint32_t>(sizeof(head)) + head[14] + 3;

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) - ownership transferred to game cache
    auto* buf = new uint8_t[head[13]];
    std::memcpy(buf, head.data(), sizeof(head));
    std::memcpy(buf + sizeof(head), rle.data(), rleLen);
    std::memset(buf + sizeof(head) + rleLen, 0xee, 3);
    return buf;
}

// Read an 8-bit indexed RGB BMP from `path` and produce a DC6 cell.
// Returns nullptr for missing files, non-conforming formats (24-bit,
// compressed, top-down), and pathological dimensions.
void* LoadFromBmp(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return nullptr;
    }
    const auto size = static_cast<std::streamsize>(file.tellg());
    constexpr std::streamsize MIN_BMP_BYTES = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    if (size < MIN_BMP_BYTES) {
        return nullptr;
    }
    file.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buf.data()), size)) {
        return nullptr;
    }

    auto* bmpHeader = reinterpret_cast<const BITMAPFILEHEADER*>(buf.data());
    auto* infoHeader = reinterpret_cast<const BITMAPINFOHEADER*>(buf.data() + sizeof(BITMAPFILEHEADER));
    if (bmpHeader->bfType != 0x4d42 /* 'BM' */ || infoHeader->biBitCount != 8 || infoHeader->biCompression != BI_RGB) {
        return nullptr;
    }
    // Refuse top-down BMPs (biHeight < 0) and pathological dimensions.
    // Reference effectively does the same - its row-iteration loop exits
    // immediately on negative height, producing an empty sprite.
    constexpr int32_t MAX_SPRITE_DIM = 8192;
    const int32_t signedW = infoHeader->biWidth;
    const int32_t signedH = infoHeader->biHeight;
    if (signedW <= 0 || signedH <= 0 || signedW > MAX_SPRITE_DIM || signedH > MAX_SPRITE_DIM) {
        return nullptr;
    }
    const auto width = static_cast<uint32_t>(signedW);
    const auto height = static_cast<uint32_t>(signedH);
    const auto offset = static_cast<size_t>(bmpHeader->bfOffBits);
    // Pixel array must lie wholly inside the file: offset + stride*height
    // must not run past the buffer end.
    const size_t stride = (static_cast<size_t>(width) + 3U) & ~size_t{3U};
    const size_t pixelBytes = stride * static_cast<size_t>(height);
    if (offset > buf.size() || pixelBytes > buf.size() - offset) {
        return nullptr;
    }
    void* raw = WrapBmpAsDc6(buf.data() + offset, width, height);
    if (!raw) {
        return nullptr;
    }
    // Register the synthetic cell with D2's resource system so the renderer
    // recognises it. Reference D2Helpers.cpp:702-706 myInitCellFile.
    if (d2cmp::D2CMP_InitCellFile.IsResolved()) {
        // The CellFile** out-parameter receives the canonicalised pointer;
        // reference uses a pointer-to-self. SourceFile/Filename markers are
        // unused except for D2-internal logging - passing literal "?".
        void* out = raw;
        d2cmp::D2CMP_InitCellFile(raw, &out, "?", 0, static_cast<uint32_t>(-1), "?");
        // After InitCellFile the canonical pointer is `out`, not `raw`.
        return out;
    }
    return raw;
}

// Generic "lookup-or-load" template: takes a key, a per-cache-table accessor,
// and a load-on-miss thunk. Caches both successful loads and failures.
template <typename Loader>
void* LookupOrLoad(std::unordered_map<std::string, void*>& table, const std::string& key, Loader load) {
    auto& cache = GlobalCache();
    {
        std::scoped_lock lock(cache.mu);
        auto it = table.find(key);
        if (it != table.end()) {
            return it->second;
        }
    }
    void* loaded = load();
    if (loaded == nullptr) {
        Logger()->warn("failed to load sprite '{}'", key);
    }
    {
        std::scoped_lock lock(cache.mu);
        // try_emplace for race safety: if another thread inserted while we
        // were loading, keep theirs (and silently leak ours - sprites are
        // tiny and the race is rare).
        auto [it, inserted] = table.try_emplace(key, loaded);
        return it->second;
    }
}

}  // namespace

std::optional<Sprite> Sprite::FromFile(const std::filesystem::path& path) {
    if (path.empty()) {
        return std::nullopt;
    }
    auto& cache = GlobalCache();
    void* p = LookupOrLoad(cache.fs, path.string(), [&] { return LoadFromBmp(path); });
    if (p == nullptr) {
        return std::nullopt;
    }
    return Sprite{p};
}

std::optional<Sprite> Sprite::FromMpq(const std::string& mpqPath) {
    if (mpqPath.empty() || !d2win::ARCHIVE_LoadCellFile.IsResolved()) {
        return std::nullopt;
    }
    auto& cache = GlobalCache();
    void* p = LookupOrLoad(cache.mpq, mpqPath, [&] { return d2win::ARCHIVE_LoadCellFile(mpqPath.c_str(), 0); });
    if (p == nullptr) {
        return std::nullopt;
    }
    return Sprite{p};
}

Size Sprite::Size() const {
    auto* cf = static_cast<D2CellFileStrc*>(cached_);
    if (cf == nullptr || cf->nFrames == 0 || cf->pGfxCells == nullptr) {
        return Size::Zero;
    }
    return {.width = cf->pGfxCells->dwWidth, .height = cf->pGfxCells->dwHeight};
}

void Sprite::Draw(Point centerPos, uint32_t color, bool isAutomap) const {
    auto* cf = static_cast<D2CellFileStrc*>(cached_);
    if (cf == nullptr || !d2gfx::D2GFX_DrawAutomapCell.IsResolved()) {
        return;
    }
    Point pos = isAutomap ? ScreenToAutomap(centerPos) : centerPos;

    CellContext ctx{};
    ctx.pCellFile = cf;

    // Reference D2Helpers.cpp:585-586: convert centre -> left-edge / bottom-edge
    // (Y grows downward; renderer wants the sprite's bottom-left baseline).
    if (cf->nFrames > 0 && cf->pGfxCells != nullptr) {
        const auto* frame = cf->pGfxCells;
        pos.x -= static_cast<int32_t>(frame->dwWidth / 2U);
        pos.y += static_cast<int32_t>(frame->dwHeight / 2U);
    }

    // Build the colour lookup table fresh each draw - slot 255 holds the
    // caller-supplied palette index. Reference uses a two-table toggle
    // baked into the CellFile struct (mylastcol/mytabno) to avoid
    // recomputing; we trade ~256 bytes of stack for thread-safety on
    // shared cells.
    std::array<uint8_t, 256> coltab{};
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by coltab.size()
    for (size_t i = 0; i < coltab.size(); ++i) {
        coltab[i] = static_cast<uint8_t>(i);
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    coltab[255] = static_cast<uint8_t>(color & 0xFFU);

    d2gfx::D2GFX_DrawAutomapCell(&ctx, static_cast<uint32_t>(pos.x), static_cast<uint32_t>(pos.y),
                                 static_cast<uint32_t>(-1), 5, coltab.data());
}

}  // namespace d2bs::game

// NOLINTEND(readability-identifier-naming)
