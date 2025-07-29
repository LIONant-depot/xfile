#include <string>

namespace xfile::unit_test
{
    //-----------------------------------------------------------------------------------------

    err syncModeTest( std::wstring_view FileName, bool bCloseFile )
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

    err asyncModeTest( std::wstring_view FileName, bool bCloseFile)
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
                        if (Err.getState() == xfile::err::state::INCOMPLETE) Err.clear();
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
                    if ( Err.getState() == xfile::err::state::INCOMPLETE) Err.clear();
                    else return Err;
                }

                // Start reading 
                for (int i = 1; i < Steps; i++)
                {
                    if (auto Err = File.ReadSpan(std::span( Buffer[i & 1].get(), DataSize)); Err )
                    {
                        if ( Err.getState() == xfile::err::state::INCOMPLETE) Err.clear();
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

    void Tests(void)
    {
        for (int i = 0; i < 2; ++i)
        {
           (void)syncModeTest( L"temp:/test.dat", i);
           (void)asyncModeTest( L"temp:/asyncMode.dat", i);
        }

        (void)syncModeTest( L"ram:/test.dat", false);
        (void)asyncModeTest( L"ram:/asyncMode.dat", false);

        int a = 22;
    }
}