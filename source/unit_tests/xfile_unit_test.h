#include <string>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <vector>

namespace xfile::unit_test
{
    //-----------------------------------------------------------------------------------------

    xerr syncModeTest( std::wstring_view FileName, bool bCloseFile )
    {
        xfile::stream           File;

        //
        // Write file
        //
        if ( auto Err = File.open(FileName, "w"); Err ) 
            return Err;

        std::string_view Header{ "TestFileHeader" };
        if (auto Err = File.WriteString(Header); Err ) 
            return Err;

        std::size_t FileSize;
        if( auto Err = File.Tell(FileSize); Err ) 
            return Err;

        std::size_t RealSize = Header.length() + 1;
        assert( FileSize == RealSize );

        std::array< std::uint32_t, 3245> Buffer{};
        for (std::uint32_t i = 0; i < Buffer.size(); i++)
        {
            Buffer[i] = i;
        }

        if ( auto Err = File.WriteSpan(std::span{Buffer}); Err )
            return Err;

         
        if (auto Err = File.Tell(FileSize); Err ) 
            return Err;

        RealSize += Buffer.size() * sizeof(std::uint32_t);
        assert(FileSize == RealSize);

        if ( auto Err = File.Write(Buffer.size()); Err )
            return Err;

        if( auto Err = File.Tell(FileSize); Err ) 
            return Err;

        RealSize += sizeof(decltype(Buffer.size()));
        assert(FileSize == RealSize);

        // Done
        if (bCloseFile) File.close();

        //
        // Clear the buffer
        //
        for (std::size_t i = 0; i < Buffer.size(); i++)
        {
            Buffer[i] = 0;
        }

        //
        // Read file
        //
        if (bCloseFile)
        {
            if ( auto Err = File.open(FileName, "r"); Err ) 
                return Err;
        }
        else
        {
            if ( auto Err = File.SeekOrigin(0); Err ) 
                return Err;
        }

        if( auto Err = File.getFileLength(FileSize); Err) 
            return Err;

        RealSize  = Header.length() + 1;
        RealSize += sizeof(std::int32_t) * Buffer.size() + sizeof(decltype(Buffer.size()));
        assert(FileSize == RealSize);

        // Read the first string
        std::string NewHeader;
        if ( auto Err = File.ReadString(NewHeader); Err) 
            return Err;

        assert(NewHeader == Header);

        // Read the buffer size
        std::size_t Position;
        if( auto Err = File.Tell(Position); Err ) 
            return Err;

        if( auto Err = File.SeekCurrent( sizeof(std::int32_t) * Buffer.size() ); Err ) 
            return Err;

        std::size_t BufferCount;
        if (auto Err = File.Read(BufferCount); Err ) 
            return Err;
        assert(BufferCount == Buffer.size());

        // Read the buffer
        if (auto Err = File.SeekOrigin(Position); Err ) 
            return Err;

        if (auto Err = File.ReadSpan(std::span(Buffer)); Err ) 
            return Err;

        for (std::int32_t i = 0; i < Buffer.size(); i++)
        {
            assert(Buffer[i] == i);
        }

        // Done
        File.close();

        return {};
    }

    //-----------------------------------------------------------------------------------------

    xerr asyncModeTest( std::wstring_view FileName, bool bCloseFile)
    {
        constexpr static int    Steps    = 10;
        constexpr static int    DataSize = 1024 * Steps;
        auto                    Buffer   = std::array{std::make_unique<std::int32_t[]>(DataSize), std::make_unique<std::int32_t[]>(DataSize)};
        xfile::stream           File;

        for( int t = 0; t < 10; t++ )
        {
            //
            // Write something
            //
            if (1)
            {
                //
                // Write Something
                //
                if ( auto Err = File.open(FileName, "w@"); Err ) 
                    return Err;

                //
                // Safe some random data in 10 steps
                //
                int k = 0;
                for( int i = 0; i < Steps; i++)
                {
                    // this is overlap/running in parallel with actual writing the file
                    for( auto& E : std::span(Buffer[i & 1].get(), DataSize) )
                    {
                        E = k++;
                    }

                    if (auto Err = File.Synchronize(true); Err) 
                        return Err;

                    if (auto Err = File.WriteSpan(std::span(Buffer[i & 1].get(), DataSize)); Err )
                    {
                        if (Err.getState<xfile::state>() == xfile::state::INCOMPLETE) Err.clear();
                        else return Err;
                    }
                }

                if ( auto Err = File.Synchronize(true); Err ) 
                    return Err;

                if (bCloseFile) File.close();
            }

            //
            // Clear buffer
            //
            for (auto& E : Buffer)
                std::fill_n(E.get(), DataSize, 0);

            //
            // Read something
            //
            if (1)
            {
                if (bCloseFile)
                {
                    if ( auto Err = File.open(FileName, "r@"); Err ) 
                        return Err;
                }
                else
                {
                    if ( auto Err = File.SeekOrigin(0); Err) 
                        return Err;
                }
                int     k = 0;

                // Read the first entry
                if (auto Err = File.ReadSpan(std::span(Buffer[0].get(), DataSize)); Err )
                {
                    if ( Err.getState<state>() == xfile::state::INCOMPLETE) Err.clear();
                    else return Err;
                }

                // Start reading 
                for (int i = 1; i < Steps; i++)
                {
                    if (auto Err = File.ReadSpan(std::span( Buffer[i & 1].get(), DataSize)); Err )
                    {
                        if ( Err.getState<state>() == xfile::state::INCOMPLETE) Err.clear();
                        else return Err;
                    }

                    // this is overlap/running in parallel with actual reading of the file
                    for (auto& E : std::span(Buffer[(i ^ 1) & 1].get(), DataSize))
                    {
                        assert(E == k++);
                    }

                    if( auto Err = File.Synchronize(true); Err ) 
                        return Err;
                }

                // Check the last thing we read            
                for (auto& E : std::span(Buffer[(Steps ^ 1) & 1].get(), DataSize ))
                {
                    assert(E == k++);
                }

                // We are done!
                File.close();
            }
        }

        return {};
    }

    //-----------------------------------------------------------------------------------------

    // The buffer device (xfile::BufferPath): reads, seeks and the length over the caller's memory; past the end fails; writing fails
    inline void bufferDeviceTest(void)
    {
        std::array<std::byte, 1000> Data;
        for (std::size_t i = 0; i < Data.size(); ++i) Data[i] = std::byte(i * 7);
        xfile::stream File;
        auto Err = File.open(xfile::BufferPath(Data), "rb");
        assert(!Err);
        std::size_t L = 0;
        Err = File.getFileLength(L);
        assert(!Err && L == Data.size());
        std::array<std::byte, 300> A;
        Err = File.ReadSpan(std::span{ A });
        assert(!Err && A[0] == Data[0] && A[299] == Data[299]);
        Err = File.SeekOrigin(700);
        assert(!Err);
        Err = File.ReadSpan(std::span{ A });
        assert(!Err && A[299] == Data[999] && !File.isEOF() && "reading up to the end exactly is not the end of file yet");
        Err = File.Synchronize(true);
        assert(!Err);
        Err = File.SeekOrigin(990);
        assert(!Err);
        Err = File.ReadSpan(std::span{ A });
        assert(!Err && A[9] == Data[999] && "a read crossing the end gets what there is (as the file devices)");
        Err = File.ReadSpan(std::span{ A });
        assert(Err && "a read past the end fails");
        Err.clear();
        File.close();
        xfile::stream W;
        Err = W.open(xfile::BufferPath(Data), "wb");
        assert(Err && "the buffer device is read only");
        Err.clear();
        std::printf("buffer device: PASS\n");
    }

    // ReadWholeFile: files of sizes around the sector and the request size (1 MB, 16 in flight: 20.5 MB needs the ring of requests to
    // turn), each read back byte for byte into one reused buffer; a missing file fails
    inline void readWholeFileTest(void)
    {
        xfile::read_buffer Buffer;
        for (const std::size_t Size : { std::size_t(0), std::size_t(1), std::size_t(4095), std::size_t(4097), std::size_t(1) << 20, (std::size_t(41) << 19) + 123, std::size_t(3000) })
        {
            std::vector<std::byte> Data(Size);
            for (std::size_t i = 0; i < Size; ++i) Data[i] = std::byte((i * 2654435761u) >> 13);
            const std::wstring Path = std::format(L"{}xfile_readwhole_{}.bin", xfile::getTempPath(), Size);
            {
                xfile::stream W;
                auto Err = W.open(Path, "wb");
                assert(!Err);
                if (Size) Err = W.WriteSpan(std::span{ Data });
                assert(!Err);
            }
            std::span<const std::byte> Out;
            auto Err = xfile::ReadWholeFile(Path, Buffer, Out);
            assert(!Err && Out.size() == Size && (Size == 0 || std::memcmp(Out.data(), Data.data(), Size) == 0));
            std::filesystem::remove(Path);
        }
        std::span<const std::byte> Out;
        auto Err = xfile::ReadWholeFile(L"temp:/no_such_file_xfile.bin", Buffer, Out);
        assert(Err && Out.empty());
        Err.clear();
        std::printf("ReadWholeFile: PASS\n");
    }

    void Tests(void)
    {
        for (int i = 0; i < 2; ++i)
        {
           (void)syncModeTest( L"temp:/test.dat", i);
           (void)asyncModeTest( L"temp:/asyncMode.dat", i);
        }

        (void)syncModeTest( L"ram:/test.dat", false);
        (void)asyncModeTest( L"ram:/asyncMode.dat", false);

        bufferDeviceTest();
        readWholeFileTest();

        int a = 22;
    }
}