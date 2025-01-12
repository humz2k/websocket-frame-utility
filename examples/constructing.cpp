#include <iostream>
#include <wsframe/wsframe.hpp>

int main() {
    wsframe::FrameFactory factory;

    // Build a FIN=1, opcode=TEXT, mask=false frame with payload
    std::string_view payload = "Hello World";
    std::string_view rawFrame =
        factory.text(/*fin=*/true, /*mask=*/false, payload);

    // rawFrame now references the serialized bytes in factory's internal
    // buffer. You can send rawFrame.data() over a socket (e.g. using send() or
    // SSL_write()).

    std::cout << "Constructed frame has length: " << rawFrame.size()
              << std::endl;
    return 0;
}