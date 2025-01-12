#include <iostream>
#include <wsframe/wsframe.hpp>

int main() {
    wsframe::FrameParser parser;

    // Suppose you receive some bytes from the network...
    // e.g. data from a non-blocking recv or SSL_read call
    std::string_view chunk1 = "\x81\x02\x48"; // partial data
    auto frameOpt1 = parser.update(chunk1);

    // Because it might be incomplete, we check if a frame was parsed
    if (!frameOpt1.has_value()) {
        std::cout << "No complete frame yet (partial read)..." << std::endl;
    }

    // Then you receive more bytes...
    std::string_view chunk2 = "\x69"; // the rest
    auto frameOpt2 = parser.update(chunk2);
    if (frameOpt2.has_value()) {
        wsframe::Frame frame = frameOpt2.value();
        std::cout << "Parsed a frame! Opcode="
                  << wsframe::Frame::opcode_to_string(frame.opcode)
                  << ", payload=" << frame.payload << std::endl;
    } else {
        std::cout << "Still incomplete..." << std::endl;
    }
}