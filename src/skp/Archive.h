#pragma once
// Bidirectional object-stream archive (MECHANICS.md section 1). One Archive either reads or writes;
// every class has one serialize function that calls the same field methods in both directions.

#include "skp/Model.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace skp {

class SkpError : public std::runtime_error {
public:
    explicit SkpError(const std::string& what) : std::runtime_error(what) {}
};

class Archive {
public:
    // Reading from bytes.
    Archive(QByteArray data, std::vector<Object>& arena);
    // Writing.
    explicit Archive(std::vector<Object>& arena);

    bool reading() const { return reading_; }
    std::size_t pos() const { return pos_; }
    std::size_t remaining() const { return reading_ ? static_cast<std::size_t>(data_.size()) - pos_ : 0; }
    const QByteArray& output() const { return out_; }

    // Class versions in effect (loaded map, or the 2017 table when writing).
    std::uint32_t ver(const std::string& cls) const;
    void setVersions(std::map<std::string, std::uint32_t> v) { versions_ = std::move(v); }
    // Writing a brand-new model: settings records that have no fields yet get zero values in
    // the exact order their serializer asks for them.
    void setFill(bool on) { fill_ = on; }
    bool fill() const { return fill_; }

    // Primitives (read into or write from the reference).
    void u8(std::uint8_t& v);
    void u16(std::uint16_t& v);
    void u32(std::uint32_t& v);
    void u64(std::uint64_t& v);
    void i32(std::int32_t& v);
    void f32(float& v);
    void f64(double& v);
    void boolean(bool& v);
    void color(Color& c);
    void point(Vec3d& p);
    void xform(Xform13& t);
    void guid(Guid& g);
    void bytes(QByteArray& b, std::size_t n);
    void blobU32(QByteArray& b);
    void string(QString& s);
    void byteArrayNumber(std::uint64_t& v);

    // Values only present in old files: read and dropped, nothing written.
    std::uint32_t legacyU32();
    bool legacyBool();
    double legacyF64();
    Ref legacyRef();

    // A tagged object reference (null, back-reference or new object).
    void ref(Ref& r);
    // An object serialized in place without a tag (obj->Serialize(ar)); creates it when reading.
    void inlineObj(Ref& slot, const std::function<Object()>& make);
    // Writes the end-of-list null reference / reads references until null.
    void refList(std::vector<ObjId>& list);

    std::vector<Object>& arena() { return arena_; }
    ObjId alloc(Object o);

private:
    std::size_t take(std::size_t n);
    std::uint8_t byteAt(std::size_t at) const;
    void putLE(std::uint64_t v, std::size_t n);
    void writeIndex(std::uint32_t idx, bool isClass);
    void serializeSlot(ObjId id);

    bool reading_;
    QByteArray data_;
    std::size_t pos_ = 0;
    QByteArray out_;
    std::vector<Object>& arena_;
    std::map<std::string, std::uint32_t> versions_;

    // read map: index -> class name (non-empty) or object id
    struct MapEntry {
        std::string cls;
        Ref obj;
    };
    std::vector<MapEntry> rmap_;
    std::map<std::string, std::uint32_t> wclass_;
    std::map<ObjId, std::uint32_t> wobj_;
    std::uint32_t wcount_ = 1;
    int depth_ = 0;
    bool fill_ = false;
};

void serializeObject(Archive& ar, Object& o);

// Ordered record fields for settings classes.
class RecIO {
public:
    RecIO(Archive& ar, Record& rec) : ar_(ar), rec_(rec) {}
    Archive& ar() { return ar_; }
    bool reading() const { return ar_.reading(); }
    std::uint32_t ver(const std::string& cls) const { return ar_.ver(cls); }

    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();
    float f32();
    double f64();
    void f64n(std::size_t n);
    bool b();
    Color c();
    QString s();
    Ref r();
    void bytes(std::size_t n);
    void blob();
    void entity();
    void drawElem();
    // Default value kept when an older file lacks the field.
    void def(Field f);
    // Peek the next field when writing (for empty-key loop ends).
    const Field* peek() const;

private:
    Field next(const char* what);
    Archive& ar_;
    Record& rec_;
    std::size_t cur_ = 0;
};

void serializeRecord(Archive& ar, Record& rec);

// Shared base-class serializers.
void ioEntity(Archive& ar, Entity& e);
void ioDrawElem(Archive& ar, DrawElem& d);
void ioCamera(Archive& ar, Camera& c);
void ioTypedValue(Archive& ar, TypedValue& tv);

}  // namespace skp
