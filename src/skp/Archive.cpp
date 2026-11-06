#include "skp/Archive.h"

#include <array>
#include <bit>
#include <utility>

namespace skp {

namespace {
template <class T>
T fromLE(const QByteArray& d, std::size_t at) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(d.at(static_cast<qsizetype>(at + i)))) << (8 * i);
    }
    return static_cast<T>(v);
}
}  // namespace

Archive::Archive(QByteArray data, std::vector<Object>& arena)
    : reading_(true), data_(std::move(data)), arena_(arena) {
    rmap_.push_back({});  // index 0 = null
}

Archive::Archive(std::vector<Object>& arena) : reading_(false), arena_(arena) {}

std::uint32_t Archive::ver(const std::string& cls) const {
    const auto it = versions_.find(cls);
    return it == versions_.end() ? 0u : it->second;
}

std::size_t Archive::take(std::size_t n) {
    if (n > static_cast<std::size_t>(data_.size()) || pos_ > static_cast<std::size_t>(data_.size()) - n) {
        throw SkpError("unexpected end of file at offset " + std::to_string(pos_));
    }
    const std::size_t p = pos_;
    pos_ += n;
    return p;
}

std::uint8_t Archive::byteAt(std::size_t at) const {
    return static_cast<std::uint8_t>(data_.at(static_cast<qsizetype>(at)));
}

void Archive::putLE(std::uint64_t v, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        out_.append(static_cast<char>(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu)));
    }
}

void Archive::u8(std::uint8_t& v) {
    if (reading_) {
        v = byteAt(take(1));
    } else {
        putLE(v, 1);
    }
}

void Archive::u16(std::uint16_t& v) {
    if (reading_) {
        v = fromLE<std::uint16_t>(data_, take(2));
    } else {
        putLE(v, 2);
    }
}

void Archive::u32(std::uint32_t& v) {
    if (reading_) {
        v = fromLE<std::uint32_t>(data_, take(4));
    } else {
        putLE(v, 4);
    }
}

void Archive::u64(std::uint64_t& v) {
    if (reading_) {
        v = fromLE<std::uint64_t>(data_, take(8));
    } else {
        putLE(v, 8);
    }
}

void Archive::i32(std::int32_t& v) {
    auto u = std::bit_cast<std::uint32_t>(v);
    u32(u);
    v = std::bit_cast<std::int32_t>(u);
}

void Archive::f32(float& v) {
    auto u = std::bit_cast<std::uint32_t>(v);
    u32(u);
    v = std::bit_cast<float>(u);
}

void Archive::f64(double& v) {
    auto u = std::bit_cast<std::uint64_t>(v);
    u64(u);
    v = std::bit_cast<double>(u);
}

void Archive::boolean(bool& v) {
    std::uint8_t b = v ? 1 : 0;
    u8(b);
    v = b != 0;
}

void Archive::color(Color& c) {
    u8(c.r);
    u8(c.g);
    u8(c.b);
    u8(c.a);
}

void Archive::point(Vec3d& p) {
    for (double& x : p) {
        f64(x);
    }
}

void Archive::xform(Xform13& t) {
    for (double& x : t) {
        f64(x);
    }
}

void Archive::guid(Guid& g) {
    for (std::uint8_t& b : g) {
        u8(b);
    }
}

void Archive::bytes(QByteArray& b, std::size_t n) {
    if (reading_) {
        const std::size_t p = take(n);
        b = data_.mid(static_cast<qsizetype>(p), static_cast<qsizetype>(n));
    } else {
        QByteArray c = b;
        c.resize(static_cast<qsizetype>(n), '\0');
        out_.append(c);
    }
}

void Archive::blobU32(QByteArray& b) {
    auto n = static_cast<std::uint32_t>(b.size());
    u32(n);
    bytes(b, n);
}

void Archive::string(QString& s) {
    if (reading_) {
        std::uint32_t n = byteAt(take(1));
        bool unicode = false;
        if (n == 0xFF) {
            std::uint16_t w = 0;
            u16(w);
            if (w == 0xFFFE) {
                unicode = true;
                n = byteAt(take(1));
                if (n == 0xFF) {
                    u16(w);
                    n = w;
                    if (w == 0xFFFF) {
                        u32(n);
                    }
                }
            } else if (w == 0xFFFF) {
                u32(n);
            } else {
                n = w;
            }
        }
        if (unicode) {
            const std::size_t p = take(static_cast<std::size_t>(n) * 2);
            QString r;
            r.reserve(static_cast<qsizetype>(n));
            for (std::uint32_t i = 0; i < n; ++i) {
                r.append(QChar(fromLE<std::uint16_t>(data_, p + 2 * static_cast<std::size_t>(i))));
            }
            s = r;
        } else {
            const std::size_t p = take(n);
            s = QString::fromLatin1(data_.mid(static_cast<qsizetype>(p), static_cast<qsizetype>(n)));
        }
    } else {
        const std::u16string units = s.toStdU16String();
        const std::size_t n = units.size();
        const std::uint8_t ff = 0xFF;
        std::uint16_t fffe = 0xFFFE;
        putLE(ff, 1);
        u16(fffe);
        if (n <= 0xFE) {
            auto b = static_cast<std::uint8_t>(n);
            u8(b);
        } else {
            putLE(ff, 1);
            if (n <= 0xFFFD) {
                auto w = static_cast<std::uint16_t>(n);
                u16(w);
            } else {
                std::uint16_t w = 0xFFFF;
                u16(w);
                auto l = static_cast<std::uint32_t>(n);
                u32(l);
            }
        }
        for (char16_t c : units) {
            auto w = static_cast<std::uint16_t>(c);
            u16(w);
        }
    }
}

void Archive::byteArrayNumber(std::uint64_t& v) {
    if (reading_) {
        std::uint8_t mask = 0;
        u8(mask);
        std::uint64_t r = 0;
        for (unsigned i = 0; i < 8; ++i) {
            if ((mask & (1u << i)) != 0) {
                std::uint8_t b = 0;
                u8(b);
                r |= static_cast<std::uint64_t>(b) << (8 * i);
            }
        }
        v = r;
    } else {
        std::uint8_t mask = 0;
        std::array<std::uint8_t, 8> bs{};
        for (unsigned i = 0; i < 8; ++i) {
            bs[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
            if (bs[i] != 0) {
                mask = static_cast<std::uint8_t>(mask | (1u << i));
            }
        }
        u8(mask);
        for (std::uint8_t b : bs) {
            if (b != 0) {
                u8(b);
            }
        }
    }
}

std::uint32_t Archive::legacyU32() {
    std::uint32_t x = 0;
    if (reading_) {
        u32(x);
    }
    return x;
}

bool Archive::legacyBool() {
    bool x = false;
    if (reading_) {
        boolean(x);
    }
    return x;
}

double Archive::legacyF64() {
    double x = 0;
    if (reading_) {
        f64(x);
    }
    return x;
}

Ref Archive::legacyRef() {
    Ref x;
    if (reading_) {
        ref(x);
    }
    return x;
}

ObjId Archive::alloc(Object o) {
    arena_.push_back(std::move(o));
    return arena_.size() - 1;
}

void Archive::serializeSlot(ObjId id) {
    if (id >= arena_.size()) {
        throw SkpError("object index out of range");
    }
    if (++depth_ > 4000) {
        throw SkpError("objects nested too deeply");
    }
    Object o = std::move(arena_[id]);
    arena_[id] = std::monostate{};
    serializeObject(*this, o);
    arena_[id] = std::move(o);
    --depth_;
}

void Archive::writeIndex(std::uint32_t idx, bool isClass) {
    if (idx > 0x7FFE) {
        std::uint16_t big = 0x7FFF;
        u16(big);
        std::uint32_t v = isClass ? (idx | 0x80000000u) : idx;
        u32(v);
    } else {
        auto v = static_cast<std::uint16_t>(isClass ? (idx | 0x8000u) : idx);
        u16(v);
    }
}

void Archive::ref(Ref& r) {
    if (reading_) {
        std::uint16_t w = 0;
        u16(w);
        std::uint32_t tag = 0;
        if (w == 0x7FFF) {
            u32(tag);
        } else {
            tag = (static_cast<std::uint32_t>(w) & 0x7FFFu) | ((static_cast<std::uint32_t>(w) & 0x8000u) << 16);
        }
        if ((tag & 0x80000000u) == 0) {
            if (tag == 0) {
                r.reset();
                return;
            }
            if (tag >= rmap_.size() || !rmap_[tag].obj) {
                throw SkpError("bad object reference " + std::to_string(tag) + " at offset " +
                               std::to_string(pos_));
            }
            r = rmap_[tag].obj;
            return;
        }
        std::string cls;
        if (w == 0xFFFF) {
            std::uint16_t schema = 0, len = 0;
            u16(schema);
            u16(len);
            const std::size_t p = take(len);
            cls = data_.mid(static_cast<qsizetype>(p), len).toStdString();
            rmap_.push_back({cls, std::nullopt});
        } else {
            const std::uint32_t idx = tag & 0x7FFFFFFFu;
            if (idx >= rmap_.size() || rmap_[idx].cls.empty()) {
                throw SkpError("bad class reference " + std::to_string(idx) + " at offset " + std::to_string(pos_));
            }
            cls = rmap_[idx].cls;
        }
        auto made = makeObjectOfClass(cls);
        if (!made) {
            throw SkpError("unknown class " + cls + " at offset " + std::to_string(pos_));
        }
        const ObjId id = alloc(std::monostate{});
        rmap_.push_back({std::string(), id});
        arena_[id] = std::move(*made);
        serializeSlot(id);
        r = id;
        return;
    }
    // writing
    if (!r) {
        std::uint16_t z = 0;
        u16(z);
        return;
    }
    const ObjId id = *r;
    if (const auto it = wobj_.find(id); it != wobj_.end()) {
        writeIndex(it->second, false);
        return;
    }
    if (id >= arena_.size()) {
        throw SkpError("dangling reference while saving");
    }
    const std::string cls = classNameOf(arena_[id]);
    if (const auto it = wclass_.find(cls); it != wclass_.end()) {
        writeIndex(it->second, true);
    } else {
        std::uint16_t tagNew = 0xFFFF;
        auto schema = static_cast<std::uint16_t>(ver(cls));
        auto len = static_cast<std::uint16_t>(cls.size());
        u16(tagNew);
        u16(schema);
        u16(len);
        out_.append(QByteArray::fromStdString(cls));
        wclass_[cls] = wcount_++;
    }
    wobj_[id] = wcount_++;
    serializeSlot(id);
}

void Archive::inlineObj(Ref& slot, const std::function<Object()>& make) {
    if (!slot) {
        slot = alloc(make());
    }
    serializeSlot(*slot);
}

void Archive::refList(std::vector<ObjId>& list) {
    if (reading_) {
        for (;;) {
            Ref x;
            ref(x);
            if (!x) {
                break;
            }
            list.push_back(*x);
        }
    } else {
        for (ObjId id : list) {
            Ref x = id;
            ref(x);
        }
        Ref none;
        ref(none);
    }
}

// ---------------------------------------------------------------- RecIO

Field RecIO::next(const char* what) {
    if (cur_ >= rec_.fields.size()) {
        throw SkpError(std::string("record ") + rec_.cls + " ran out writing " + what);
    }
    return rec_.fields[cur_++];
}

const Field* RecIO::peek() const {
    return cur_ < rec_.fields.size() ? &rec_.fields[cur_] : nullptr;
}

void RecIO::def(Field f) {
    if (ar_.reading()) {
        rec_.fields.push_back(std::move(f));
    }
}

namespace {
template <class T>
T fieldAs(const Field& f, const std::string& cls, const char* what) {
    if (const T* p = std::get_if<T>(&f)) {
        return *p;
    }
    throw SkpError("record " + cls + ": field type mismatch, wanted " + what);
}
}  // namespace

#define SKP_RECFIELD(NAME, TYPE, CALL)                       \
    TYPE RecIO::NAME() {                                     \
        TYPE x{};                                            \
        if (ar_.reading()) {                                 \
            ar_.CALL(x);                                     \
            rec_.fields.emplace_back(x);                     \
        } else {                                             \
            if (ar_.fill() && cur_ >= rec_.fields.size())     \
                rec_.fields.emplace_back(TYPE{});            \
            x = fieldAs<TYPE>(next(#NAME), rec_.cls, #NAME); \
            ar_.CALL(x);                                     \
        }                                                    \
        return x;                                            \
    }

SKP_RECFIELD(u8, std::uint8_t, u8)
SKP_RECFIELD(u16, std::uint16_t, u16)
SKP_RECFIELD(u32, std::uint32_t, u32)
SKP_RECFIELD(u64, std::uint64_t, u64)
SKP_RECFIELD(f32, float, f32)
SKP_RECFIELD(f64, double, f64)
SKP_RECFIELD(b, bool, boolean)
SKP_RECFIELD(c, Color, color)
SKP_RECFIELD(s, QString, string)
SKP_RECFIELD(r, Ref, ref)
#undef SKP_RECFIELD

void RecIO::f64n(std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        f64();
    }
}

void RecIO::bytes(std::size_t n) {
    QByteArray x;
    if (ar_.reading()) {
        ar_.bytes(x, n);
        rec_.fields.emplace_back(x);
    } else {
        if (ar_.fill() && cur_ >= rec_.fields.size()) rec_.fields.emplace_back(QByteArray(static_cast<qsizetype>(n), char{0}));
        x = fieldAs<QByteArray>(next("bytes"), rec_.cls, "bytes");
        ar_.bytes(x, n);
    }
}

void RecIO::blob() {
    const std::uint32_t n = u32();
    bytes(n);
}

void RecIO::entity() {
    const std::uint32_t v = ar_.ver("CEntity");
    if (v == 1) {
        ar_.legacyU32();
    }
    if (v > 2) {
        r();
    } else {
        def(Ref{});
    }
    if (v == 4) {
        u64();
    } else if (v >= 5) {
        std::uint64_t x = 0;
        if (ar_.reading()) {
            ar_.byteArrayNumber(x);
            rec_.fields.emplace_back(x);
        } else {
            if (ar_.fill() && cur_ >= rec_.fields.size()) rec_.fields.emplace_back(std::uint64_t{0});
            x = fieldAs<std::uint64_t>(next("pid"), rec_.cls, "pid");
            ar_.byteArrayNumber(x);
        }
    } else {
        def(std::uint64_t{0});
    }
}

void RecIO::drawElem() {
    entity();
    const std::uint32_t v = ar_.ver("CDrawingElement");
    if (v > 4 || v == 2) {
        r();
    } else {
        if (ar_.reading()) {
            Color c{};
            ar_.color(c);
        }
        def(Ref{});
    }
    if (v != 0) {
        b();
    } else {
        def(false);
    }
    if (v >= 6) {
        b();
        b();
    } else {
        def(true);
        def(true);
    }
    for (std::uint32_t k = 7; k <= 9; ++k) {
        if (v >= k) {
            b();
        } else {
            def(false);
        }
    }
    if (v == 2 && ar_.legacyBool()) {
        ar_.legacyRef();
    }
    if (v >= 4) {
        r();
    } else {
        def(Ref{});
    }
}

}  // namespace skp
