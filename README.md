# websocket-frame-utility

Single-header C++ library for fast parsing and constructing of Websocket frames.

**Disclaimer: this readme is largely AI generated, so mistakes may exist**.

> **Note**:
> - This library does **not** implement the full WebSocket handshake (HTTP Upgrade, etc.).
> - It also does **not** handle **TLS**/**SSL** or **fragmentation** of large messages.
> - The parser, as written, does **not** automatically unmask frames—if you receive a masked frame (client-to-server), you’ll see the payload in its masked form unless you unmask it manually.

Below is an overview of how to build frames with `FrameFactory` and parse them incrementally with `FrameParser`.

## Features

1. **Frame Construction**: Easily build **text**, **binary**, **ping**, **pong**, and **close** frames.
2. **Incremental Parsing**: Pass partial data to `FrameParser::update(...)` repeatedly. The parser accumulates data in a buffer until a complete frame is recognized.
3. **Payload Size Handling**: Supports extended payload lengths (16-bit and 64-bit).
4. **Masking**: `FrameFactory` can generate random 4-byte masking keys and XOR the payload for **client-to-server** frames.

---

## Installation

Since **wsframe** is a **header-only** library, you can simply copy `wsframe.hpp` into your project’s include path (or just use the included `CMakeLists.txt`). There are **no external dependencies** beyond the C++ standard library (and `std::random_device`, `std::mt19937`).

```bash
git clone https://github.com/yourname/wsframe.git
# or copy wsframe.hpp into your include folder
```

Then in your C++ files:
```cpp
#include "path/to/wsframe.hpp"
```

Compile with a C++17 (or higher) compiler:
```bash
g++ -std=c++17 main.cpp -o myapp
```

---

## Usage Overview

### Constructing Frames

Use the **`FrameFactory`** to build a single **WebSocket frame** with a specific opcode and optional payload. For example, to construct a **text** frame with the message `"Hello World"`:

```cpp
#include <wsframe/wsframe.hpp>
#include <iostream>

int main() {
    wsframe::FrameFactory factory;

    // Build a FIN=1, opcode=TEXT, mask=false frame with payload
    std::string_view payload = "Hello World";
    std::string_view rawFrame = factory.text(/*fin=*/true, /*mask=*/false, payload);

    // rawFrame now references the serialized bytes in factory's internal buffer.
    // You can send rawFrame.data() over a socket (e.g. using send() or SSL_write()).

    std::cout << "Constructed frame has length: " << rawFrame.size() << std::endl;
    return 0;
}
```

Other frame types:
- `factory.binary(fin, mask, payload)`
- `factory.ping(mask, payload)`
- `factory.pong(mask, payload)`
- `factory.close(mask, payload)`

> **Note**:
> - **Control frames** (`ping`, `pong`, `close`) must have payload **≤ 125 bytes** (RFC requirement).
> - **`mask=true`** is typically used by **clients** sending data to a server. If you’re writing server code, you usually set `mask=false` for server-to-client frames.

### Parsing Frames

Use **`FrameParser`** to **incrementally** parse frames. Call `update(...)` with chunks of data (e.g., from `recv()` or `SSL_read()`). The parser buffers partial data until a full frame is recognized, then returns a `std::optional<Frame>`.

```cpp
#include <wsframe/wsframe.hpp>
#include <iostream>

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
```

- `parser.clear()` can be used to clear the internal buffer and prepare for a fresh parse if you detect a protocol error or want to discard leftover data.
- If the parser completes a frame, any leftover bytes remain in the buffer, and can be used to parse subsequent frames.

---

## Code Structure

1. **`XorShift128Plus`**
   - A fast, non-cryptographic pseudo-random number generator used to generate masking keys.

2. **`FrameBuffer`**
   - A resizable buffer that stores raw bytes. It provides methods like `push_back(...)` and `get_space(...)` to append data efficiently.
   - Can produce **views** (`FrameBuffer::View` or `std::string_view`) referencing the internal buffer.

3. **`Frame`**
   - Represents a single WebSocket frame with fields: `fin`, `mask`, `opcode`, `masking_key`, and `payload`.
   - `Frame::construct()` writes its data into a `FrameBuffer`.

4. **`FrameFactory`**
   - Wraps a `FrameBuffer` and a small random cache.
   - Provides high-level methods like `text(...)`, `binary(...)`, `ping(...)`, etc. to build a frame and return a `std::string_view` of the serialized bytes.

5. **`FrameParser`**
   - An incremental parser that accumulates data from `update(...)` calls.
   - Once enough data is present to form a full frame, it returns `std::optional<Frame>`. Otherwise, it returns an empty `std::optional`.

---

## Limitations

1. **No Handshake Layer**: This is **not** a full WebSocket client/server library. It only handles **binary framing** once the handshake is done.
2. **No Automatic Unmasking**: The parser does not unmask inbound frames. If `mask=true`, you’ll see masked bytes in `Frame::payload`.
3. **No Fragmentation Support**: The code does **not** handle multi-frame fragmentation (FIN=0, continuation frames). For production usage, you’d need to handle or reassemble fragments.
4. **No TLS**: The code does not manage TLS sockets; you’d wrap it in your own SSL/TCP logic.