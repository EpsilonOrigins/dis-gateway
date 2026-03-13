#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <arpa/inet.h>

// ---------------------------------------------------------------------------
// Endian helpers
// ---------------------------------------------------------------------------
namespace detail {
    inline bool is_le() {
        uint16_t v = 1;
        return *reinterpret_cast<const uint8_t*>(&v) == 1;
    }
    inline uint64_t bswap64(uint64_t v) { return __builtin_bswap64(v); }
}

// ---------------------------------------------------------------------------
// ReadBuffer – wraps a raw byte span, reads DIS (big-endian) fields
// ---------------------------------------------------------------------------
class ReadBuffer {
public:
    ReadBuffer(const uint8_t* data, std::size_t len)
        : data_(data), len_(len), pos_(0) {}

    std::size_t remaining() const { return len_ - pos_; }
    std::size_t position()  const { return pos_; }
    const uint8_t* ptr()    const { return data_ + pos_; }

    void skip(std::size_t n) { need(n); pos_ += n; }

    uint8_t read_u8() {
        need(1);
        return data_[pos_++];
    }
    int8_t read_i8() { return static_cast<int8_t>(read_u8()); }

    uint16_t read_u16() {
        need(2);
        uint16_t v; std::memcpy(&v, data_ + pos_, 2); pos_ += 2;
        return ntohs(v);
    }
    int16_t read_i16() { return static_cast<int16_t>(read_u16()); }

    uint32_t read_u32() {
        need(4);
        uint32_t v; std::memcpy(&v, data_ + pos_, 4); pos_ += 4;
        return ntohl(v);
    }
    int32_t read_i32() { return static_cast<int32_t>(read_u32()); }

    float read_f32() {
        uint32_t raw = read_u32();
        float v; std::memcpy(&v, &raw, 4); return v;
    }

    double read_f64() {
        need(8);
        uint64_t raw; std::memcpy(&raw, data_ + pos_, 8); pos_ += 8;
        if (detail::is_le()) raw = detail::bswap64(raw);
        double v; std::memcpy(&v, &raw, 8); return v;
    }

    void read_bytes(uint8_t* dst, std::size_t n) {
        need(n); std::memcpy(dst, data_ + pos_, n); pos_ += n;
    }

    // Read n bytes into a std::string (useful for entity marking, etc.)
    std::string read_string(std::size_t n) {
        need(n);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }

private:
    const uint8_t* data_;
    std::size_t    len_, pos_;

    void need(std::size_t n) const {
        if (pos_ + n > len_)
            throw std::runtime_error("ReadBuffer underflow at offset " +
                                     std::to_string(pos_));
    }
};

// ---------------------------------------------------------------------------
// WriteBuffer – builds a DIS (big-endian) byte stream
// ---------------------------------------------------------------------------
class WriteBuffer {
public:
    void write_u8(uint8_t v)  { buf_.push_back(v); }
    void write_i8(int8_t v)   { write_u8(static_cast<uint8_t>(v)); }

    void write_u16(uint16_t v) {
        v = htons(v);
        auto p = reinterpret_cast<const uint8_t*>(&v);
        buf_.insert(buf_.end(), p, p + 2);
    }
    void write_i16(int16_t v) { write_u16(static_cast<uint16_t>(v)); }

    void write_u32(uint32_t v) {
        v = htonl(v);
        auto p = reinterpret_cast<const uint8_t*>(&v);
        buf_.insert(buf_.end(), p, p + 4);
    }
    void write_i32(int32_t v) { write_u32(static_cast<uint32_t>(v)); }

    void write_f32(float v) {
        uint32_t raw; std::memcpy(&raw, &v, 4); write_u32(raw);
    }

    void write_f64(double v) {
        uint64_t raw; std::memcpy(&raw, &v, 8);
        if (detail::is_le()) raw = detail::bswap64(raw);
        auto p = reinterpret_cast<const uint8_t*>(&raw);
        buf_.insert(buf_.end(), p, p + 8);
    }

    void write_bytes(const uint8_t* src, std::size_t n) {
        buf_.insert(buf_.end(), src, src + n);
    }

    void write_string(const std::string& s, std::size_t fixed_len) {
        std::size_t copy = std::min(s.size(), fixed_len);
        buf_.insert(buf_.end(),
                    reinterpret_cast<const uint8_t*>(s.data()),
                    reinterpret_cast<const uint8_t*>(s.data()) + copy);
        for (std::size_t i = copy; i < fixed_len; ++i) buf_.push_back(0);
    }

    void write_zeros(std::size_t n) {
        buf_.insert(buf_.end(), n, 0);
    }

    // Patch a uint16 field that was not yet known when first written
    // (used to fill in the PDU length field).
    void patch_u16(std::size_t offset, uint16_t v) {
        uint16_t be = htons(v);
        std::memcpy(buf_.data() + offset, &be, 2);
    }

    const std::vector<uint8_t>& data() const { return buf_; }
    std::size_t size() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
};
