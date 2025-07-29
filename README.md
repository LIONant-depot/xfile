# xfile: Revolutionize Your File Handling in C++!

Unleash the power of seamless, high-performance file I/O with **xfile** – a cutting-edge C++ library 
that transcends traditional file operations! Say goodbye to fopen limitations and hello to a versatile 
stream class supporting multiple devices (from local drives to network and RAM), async access, compression, 
and more. Built for speed and flexibility, xfile empowers developers to handle data like never before!

## Key Features
- **Multi-Device Mastery**: Effortlessly access files across multiple devices
- **Advanced Modes**: Read/write in binary/text/Unicode, async ops, compression, and precise seeking – all in one powerful stream interface!
- **Error-Proof & Efficient**: Fast and clear error handling thanks to C++20 constexpr
- **Async & Sync Harmony**: Synchronize or abort async tasks on demand, plus force-flush for debugging prowess!
- **Printf-Style Magic**: Built-in formatted printing for strings and wide chars
- **MIT License**: As free as it gets
- **No Dependencies**: No extra libraries needed
- **Eeasy to integrate**: Simply add to your project ```xfile.cpp``` and ```xfile.h``` and you are done.

## Code Example
```cpp
#include "xfile.h"

int main() {
    xfile::stream file;
    if( auto Err = file.open(L"temp:\\example.bin", "w+"); Err)  // Create and write binary file
    {   // Handle error 
        return 1; 
    }

    std::array<int, 3> data = {1, 2, 3};
    file.WriteSpan(data);  // Write trivial data span

    file.SeekOrigin(0);    // Reset to start
    std::array<int, 3> readData;
    file.ReadSpan(readData);  // Read back

    file.close();
    return 0;
}
```

Dive in and transform your I/O game – star, fork, and contribute now! 🚀