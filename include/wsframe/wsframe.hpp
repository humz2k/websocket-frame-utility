#ifndef _WSFRAME_WSFRAME_HPP_
#define _WSFRAME_WSFRAME_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wsframe {

// slow! use for seeds
inline uint64_t device_random() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<std::uint64_t> dist;
    return dist(rng);
}

class XorShift128Plus {
  public:
    // Seeds (64-bit each). Make sure they're not both zero.
    // You can seed them however you like.
    std::array<std::uint64_t, 2> s;

    XorShift128Plus(std::uint64_t seed1, std::uint64_t seed2) {
        if (seed1 == 0 && seed2 == 0)
            seed2 = 1;
        s[0] = seed1;
        s[1] = seed2;
    }

    // Generate next 64-bit random value
    std::uint64_t next64() {
        std::uint64_t x = s[0];
        std::uint64_t const y = s[1];
        s[0] = y;
        x ^= x << 23;
        s[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
        return s[1] + y;
    }

    void fill_bytes(std::uint8_t* buf, std::size_t n) {
        while (n >= 8) {
            std::uint64_t rnd = next64();
            // Copy 8 bytes of rnd into buf
            std::memcpy(buf, &rnd, 8);
            buf += 8;
            n -= 8;
        }
        // Handle leftover bytes
        if (n > 0) {
            uint64_t rnd = next64();
            std::memcpy(buf, &rnd, n);
        }
    }

    template <typename T, size_t n> void fill_bytes(std::array<T, n>& buf) {
        fill_bytes(static_cast<uint8_t*>(buf.data()), n * sizeof(T));
    }
};

class FrameBuffer {
  private:
    std::vector<std::uint8_t> m_buf;
    std::size_t m_ptr = 0;

  public:
    class View {
      private:
        const std::uint8_t* m_buf;
        const std::size_t m_sz;

      public:
        View(const std::uint8_t* buf, std::size_t sz) : m_buf(buf), m_sz(sz) {}
        std::size_t size() const { return m_sz; }
        const std::uint8_t* buf() const { return m_buf; }
        const std::uint8_t* begin() const { return m_buf; }
        const std::uint8_t* end() const { return m_buf + m_sz; }
    };

    FrameBuffer(std::size_t initial_capacity = 4096)
        : m_buf(initial_capacity) {}

    std::size_t capacity() const { return m_buf.size(); }

    void reset() { m_ptr = 0; }

    void ensure_fit(std::size_t sz) {
        if (capacity() < sz) {
            m_buf.resize(sz);
        }
    }

    void ensure_extra_space(std::size_t extra) { ensure_fit(m_ptr + extra); }

    // no bounds checking
    void push_back(uint8_t byte) {
        m_buf[m_ptr] = byte;
        m_ptr++;
    }

    // no bounds checking
    std::uint8_t* get_space(std::size_t sz) {
        std::uint8_t* out = &m_buf[m_ptr];
        m_ptr += sz;
        return out;
    }

    void claim_space(std::size_t sz) { m_ptr += sz; }

    void push_back(const View& view) {
        ensure_extra_space(view.size());
        std::memcpy(get_space(view.size()), view.buf(),
                    view.size() * sizeof(std::uint8_t));
    }

    void push_back(const std::string_view& view) {
        ensure_fit(m_ptr + view.size());
        std::memcpy(get_space(view.size()), view.data(),
                    view.size() * sizeof(char));
    }

    std::uint8_t* head() { return m_buf.data(); }
    const std::uint8_t* head() const { return m_buf.data(); }

    std::uint8_t* tail() { return &m_buf[m_ptr]; }
    const std::uint8_t* tail() const { return &m_buf[m_ptr]; }

    std::size_t size() const { return m_ptr; }

    template <typename T> T view() const;
};

template <>
inline FrameBuffer::View FrameBuffer::view<FrameBuffer::View>() const {
    return View(m_buf.data(), m_ptr);
}

template <>
inline std::string_view FrameBuffer::view<std::string_view>() const {
    return std::string_view((const char*)m_buf.data(), m_ptr);
}

struct Frame {
    enum class Opcode : uint8_t {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA,
        UNKNOWN
    };

    static inline const char* opcode_to_string(Opcode code) {
        switch (code) {
        case Opcode::CONTINUATION:
            return "Opcode::CONTINUATION";
        case Opcode::TEXT:
            return "Opcode::TEXT";
        case Opcode::BINARY:
            return "Opcode::BINARY";
        case Opcode::CLOSE:
            return "Opcode::CLOSE";
        case Opcode::PING:
            return "Opcode::PING";
        case Opcode::PONG:
            return "Opcode::PONG";
        default:
            return "Opcode::UNKNOWN";
        }
    }

    bool fin;
    bool mask;
    Opcode opcode;
    std::array<std::uint8_t, 4> masking_key;
    std::string_view payload;

    friend std::ostream& operator<<(std::ostream& stream, const Frame& frame) {
        stream << "[fin=" << frame.fin << "]["
               << Frame::opcode_to_string(frame.opcode)
               << "][mask=" << frame.mask << "]";
        if (frame.mask) {
            stream << "[key=" << std::hex << frame.masking_key[0] << " "
                   << frame.masking_key[1] << " " << frame.masking_key[2] << " "
                   << frame.masking_key[3] << std::dec << "]";
        }
        if (frame.payload.size() > 0) {
            stream << "[payload=\"" << frame.payload << "\"]";
        }
        return stream;
    }

  protected:
    void construct(FrameBuffer& buf) const {
        buf.reset();
        buf.ensure_fit(payload.length() + 14 + 100);

        // fin bit + 3 rsv bits + opcode
        buf.push_back(
            ((fin ? 0x80 : 0x00) | (static_cast<std::uint8_t>(opcode) & 0x0F)));

        // mask bit + payload length
        //   if payload.len < 126, len fits in 7 bits
        //   if 126 <= payload.len < 65526, write 126, then 16-bit length
        //   if payload.len >= 65536, write 127, then 64-bit length
        const std::uint8_t mask_bit = mask ? 0x80 : 0x00;

        std::uint64_t payload_length = payload.length();
        auto* payload_data = payload.data();

        if (payload_length < 126U) {
            buf.push_back(mask_bit | static_cast<std::uint8_t>(payload_length));
        } else if (payload_length <= 0xFFFFU) {
            buf.push_back(mask_bit | 126U);
            // write length in network order (big-endian)
            buf.push_back(
                static_cast<std::uint8_t>((payload_length >> 8) & 0xFFU));
            buf.push_back(static_cast<std::uint8_t>(payload_length & 0xFFU));
        } else {
            buf.push_back(mask_bit | 127U);
            // write length in big-endian
            std::uint8_t* ptr = buf.get_space(8);
            for (int i = 7; i >= 0; i--) {
                *ptr = static_cast<std::uint8_t>((payload_length >> (8 * i)) &
                                                 0xFFU);
                ptr++;
            }
        }

        // if mask, write the mask bytes and then xor payload bytes with that
        if (mask) {

            std::memcpy(buf.get_space(4), masking_key.data(),
                        masking_key.size() * sizeof(std::uint8_t));
            std::uint8_t* ptr = buf.get_space(payload_length);
            for (std::size_t i = 0; i < payload_length; i++) {
                ptr[i] = payload_data[i] ^ masking_key[i % 4];
            }
        } else {
            // otherwise, just write payload
            std::memcpy(buf.get_space(payload_length), payload_data,
                        payload_length * sizeof(std::uint8_t));
        }
    }
    friend class FrameFactory;
};

class FrameFactory {
  private:
    template <int entries> class RandomCache {
      private:
        XorShift128Plus m_random;
        std::array<std::uint8_t, entries * 4> m_cache;
        std::size_t m_cache_ptr = 0;

      public:
        RandomCache() : m_random(device_random(), device_random()) {
            fill_cache();
        }

        void fill_cache() {
            m_random.fill_bytes(m_cache);
            m_cache_ptr = 0;
        }

        void get(std::array<uint8_t, 4>& ptr) {
            if (m_cache_ptr >= entries * 4) {
                fill_cache();
            }
            std::copy(&m_cache[m_cache_ptr], &m_cache[m_cache_ptr + 4],
                      &ptr[0]);
            m_cache_ptr += 4;
        }
    };

    FrameBuffer m_buf;
    RandomCache<8> m_random;

  public:
    FrameFactory(std::size_t initial_capacity = 4096)
        : m_buf(initial_capacity) {}

    void fill_random_cache() { m_random.fill_cache(); }

    std::string_view construct(bool fin, Frame::Opcode opcode, bool mask,
                               std::string_view payload) {
        Frame frame;
        frame.fin = fin;
        frame.mask = mask;
        frame.opcode = opcode;
        if (mask) {
            m_random.get(frame.masking_key);
        }
        frame.payload = payload;
        frame.construct(m_buf);
        return m_buf.view<std::string_view>();
    }

    std::string_view text(bool fin, bool mask, std::string_view payload) {
        return construct(fin, Frame::Opcode::TEXT, mask, payload);
    }

    std::string_view binary(bool fin, bool mask, std::string_view payload) {
        return construct(fin, Frame::Opcode::BINARY, mask, payload);
    }

    std::string_view ping(bool mask, std::string_view payload) {
        if (payload.size() > 125) {
            throw std::runtime_error(
                "Payload should be <= 125 for ping frames");
        }
        return construct(true, Frame::Opcode::PING, mask, payload);
    }

    std::string_view pong(bool mask, std::string_view payload) {
        if (payload.size() > 125) {
            throw std::runtime_error(
                "Payload should be <= 125 for pong frames");
        }
        return construct(true, Frame::Opcode::PONG, mask, payload);
    }

    std::string_view close(bool mask, std::string_view payload) {
        if (payload.size() > 125) {
            throw std::runtime_error(
                "Payload should be <= 125 for close frames");
        }
        return construct(true, Frame::Opcode::CLOSE, mask, payload);
    }
};

class FrameParser {
  private:
    enum class ParseStage {
        FIN_BIT,
        OPCODE,
        MASK_BIT,
        PAYLOAD_LEN,
        EXTENDED_PAYLOAD_LEN_16,
        EXTENDED_PAYLOAD_LEN_64,
        MASKING_KEY,
        PAYLOAD_DATA,
        DONE
    };
    ParseStage m_parse_stage = ParseStage::FIN_BIT;
    Frame m_frame;
    FrameBuffer m_frame_buffer;
    std::uint64_t m_payload_len = 0;
    std::size_t m_ptr = 0;

    std::size_t remaining() const { return m_frame_buffer.size() - m_ptr; }

    std::uint8_t read() const { return *(m_frame_buffer.head() + m_ptr); }

    std::uint8_t consume() {
        auto out = read();
        m_ptr++;
        return out;
    }

    void check_fin_bit() {
        if ((m_parse_stage != ParseStage::FIN_BIT) || (remaining() == 0))
            return;
        m_frame.fin = read() & 0x80;
        m_parse_stage = ParseStage::OPCODE;
    }

    void check_opcode() {
        if ((m_parse_stage != ParseStage::OPCODE) || (remaining() == 0))
            return;
        m_frame.opcode = static_cast<Frame::Opcode>(consume() & 0x0F);
        m_parse_stage = ParseStage::MASK_BIT;
    }

    void check_mask_bit() {
        if ((m_parse_stage != ParseStage::MASK_BIT) || (remaining() == 0))
            return;
        m_frame.mask = read() & 0x80;
        m_parse_stage = ParseStage::PAYLOAD_LEN;
    }

    void check_payload_len() {
        if ((m_parse_stage != ParseStage::PAYLOAD_LEN) || (remaining() == 0))
            return;
        std::size_t len = consume() & 0x7F;
        if (len == 126) {
            m_parse_stage = ParseStage::EXTENDED_PAYLOAD_LEN_16;
            return;
        }
        if (len == 127) {
            m_parse_stage = ParseStage::EXTENDED_PAYLOAD_LEN_64;
            return;
        }
        m_payload_len = len;
        m_parse_stage =
            m_frame.mask ? ParseStage::MASKING_KEY : ParseStage::PAYLOAD_DATA;
    }

    void check_extended_payload_len_16() {
        if ((m_parse_stage != ParseStage::EXTENDED_PAYLOAD_LEN_16) ||
            (remaining() < 2))
            return;
        uint64_t left = consume();
        uint64_t right = consume();
        m_payload_len = (left << 8) | right;
        m_parse_stage =
            m_frame.mask ? ParseStage::MASKING_KEY : ParseStage::PAYLOAD_DATA;
    }

    void check_extended_payload_len_64() {
        if ((m_parse_stage != ParseStage::EXTENDED_PAYLOAD_LEN_64) ||
            (remaining() < 8))
            return;
        m_payload_len = 0;
        for (int i = 7; i >= 0; i--) {
            m_payload_len |= consume() << 8 * i;
        }
        m_parse_stage =
            m_frame.mask ? ParseStage::MASKING_KEY : ParseStage::PAYLOAD_DATA;
    }

    void check_masking_key() {
        if ((m_parse_stage != ParseStage::MASKING_KEY) || (remaining() < 4))
            return;
        for (int i = 0; i < 4; i++) {
            m_frame.masking_key[i] = consume();
        }
        m_parse_stage = ParseStage::PAYLOAD_DATA;
    }

    void check_payload_data() {
        if ((m_parse_stage != ParseStage::PAYLOAD_DATA) ||
            (remaining() < m_payload_len))
            return;
        if (m_payload_len == 0) {
            m_parse_stage = ParseStage::DONE;
            return;
        }
        const char* buf = (const char*)(m_frame_buffer.head() + m_ptr);
        m_frame.payload = std::string_view(buf, m_payload_len);
        m_ptr += m_payload_len;
        m_parse_stage = ParseStage::DONE;
    }

    bool done() const { return m_parse_stage == ParseStage::DONE; }

    std::optional<Frame> parse() {
        check_fin_bit();
        check_opcode();
        check_mask_bit();
        check_payload_len();
        check_extended_payload_len_16();
        check_extended_payload_len_64();
        check_masking_key();
        check_payload_data();
        if (!done())
            return {};
        return m_frame;
    }

    void reset() {
        if (remaining() > 0) {
            auto previous =
                FrameBuffer::View(m_frame_buffer.head() + m_ptr, remaining());
            m_frame_buffer.reset();
            m_frame_buffer.push_back(previous);
            m_ptr = 0;
        } else {
            m_frame_buffer.reset();
            m_ptr = 0;
        }

        m_frame = {};
        m_payload_len = 0;
        m_parse_stage = ParseStage::FIN_BIT;
    }

  public:
    FrameParser() {}

    void clear() {
        m_frame_buffer.reset();
        m_ptr = 0;
        m_frame = {};
        m_payload_len = 0;
        m_parse_stage = ParseStage::FIN_BIT;
    }

    std::optional<Frame> update(const FrameBuffer::View& view) {
        if (done())
            reset();
        if (view.size() != 0)
            m_frame_buffer.push_back(view);
        else if (m_parse_stage != ParseStage::FIN_BIT)
            return {};
        if (remaining() == 0)
            return {};
        return parse();
    }

    std::optional<Frame> update(const std::string_view& view) {
        if (done())
            reset();
        if (view.size() != 0)
            m_frame_buffer.push_back(view);
        else if (m_parse_stage != ParseStage::FIN_BIT)
            return {};
        if (remaining() == 0)
            return {};
        return parse();
    }

    std::optional<Frame> update(bool new_data) {
        if (done())
            reset();
        if ((!new_data) && (m_parse_stage != ParseStage::FIN_BIT))
            return {};
        if (remaining() == 0)
            return {};
        return parse();
    }

    wsframe::FrameBuffer& frame_buffer() { return m_frame_buffer; }
};

} // namespace wsframe

#endif // _WSFRAME_WSFRAME_HPP_