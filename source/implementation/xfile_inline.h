#include <cassert>

namespace xfile 
{
    namespace details
    {
        constexpr 
        std::size_t Align(std::size_t Address, const int AlignTo) noexcept
        {
            return (Address + (static_cast<std::size_t>(AlignTo) - 1)) & static_cast<std::size_t>(-AlignTo);
        }
    }

    //------------------------------------------------------------------------------

    stream::~stream(void) noexcept
    {
        if (m_pInstance) close();
    }

    //------------------------------------------------------------------------------

    template<class T> requires std::is_trivial_v<T> 
    err stream::Write(const T& Val) noexcept
    {
        assert(m_pInstance);
        return WriteRaw({ reinterpret_cast<const std::byte*>(&Val), sizeof(T)});
    }

    //------------------------------------------------------------------------------

    template<typename T> requires std::is_trivial_v<T>
    err stream::WriteSpan(std::span<T> A) noexcept
    {
        assert(m_pInstance);
        return WriteRaw({ reinterpret_cast< const std::byte*>(A.data()), sizeof( decltype(A[0]) ) * A.size() });
    }

    //------------------------------------------------------------------------------

    template<typename T, std::size_t T_COUNT_V>  requires std::is_trivial_v<T>
    err stream::WriteSpan(std::span<T, T_COUNT_V> A) noexcept
    {
        assert(m_pInstance);
        return WriteRaw({ reinterpret_cast<const std::byte*>(A.data()), sizeof(decltype(A[0])) * T_COUNT_V });
    }

    //------------------------------------------------------------------------------

    template<class T> requires std::is_trivial_v<T>
    err stream::Read( T& Val ) noexcept
    {
        assert(m_pInstance);
        return ReadRaw({ reinterpret_cast<std::byte*>(&Val), sizeof(T) });
    }

    //------------------------------------------------------------------------------

    template<typename T> requires std::is_trivial_v<T>
    err stream::ReadSpan(std::span<T> A) noexcept
    {
        assert(m_pInstance);
        return ReadRaw({ reinterpret_cast<std::byte*>(A.data()), sizeof(decltype(A[0])) * A.size() });
    }

    //------------------------------------------------------------------------------

    template<class T, std::size_t T_COUNT_V>  requires std::is_trivial_v<T>
    err stream::ReadSpan(std::span<T, T_COUNT_V> A)                                    noexcept
    {
        assert(m_pInstance);
        return ReadRaw({ reinterpret_cast<std::byte*>(A.data()), sizeof(decltype(A[0])) * T_COUNT_V });
    }


    //------------------------------------------------------------------------------
    inline
    bool stream::isBinaryMode( void ) const noexcept
    {
        assert(m_pInstance);
        return m_AccessType.m_Text == 0;
    }

    //------------------------------------------------------------------------------
    inline
    bool stream::isReadMode( void ) const noexcept
    {
        assert(m_pInstance);
        return m_AccessType.m_bRead;
    }

    //------------------------------------------------------------------------------
    inline
    bool stream::isWriteMode(void) const noexcept
    {
        assert(m_pInstance);
        return m_AccessType.m_bWrite;
    }

    //------------------------------------------------------------------------------
    inline
    void stream::setForceFlush(bool bOnOff) noexcept
    {
        assert(m_pInstance);
        m_AccessType.m_bForceFlush = bOnOff;
    }

    //------------------------------------------------------------------------------
    inline
    void stream::AsyncAbort(void) noexcept
    {
        assert(m_pInstance);
        if( m_AccessType.m_bASync == false ) return;\
        m_pInstance->AsyncAbort();
    }

    //------------------------------------------------------------------------------
    inline
    err stream::Synchronize(bool bBlock) noexcept
    {
        assert(m_pInstance);
        if( m_AccessType.m_bASync == false )
        {
            if (isEOF()) return err::create<err::state::UNEXPECTED_EOF, "Synchronize end of file">();
            return {};
        }

        return m_pInstance->Synchronize(bBlock);
    }

    //------------------------------------------------------------------------------
    inline
    void stream::Flush(void) noexcept
    {
        assert(m_pInstance);
        m_pInstance->Flush();
    }

    //------------------------------------------------------------------------------
    inline
    err stream::SeekOrigin( std::size_t Offset ) noexcept
    {
        assert(m_pInstance);
        return m_pInstance->Seek( device::SKM_ORIGIN, Offset );
    }

    //------------------------------------------------------------------------------
    inline
    err stream::SeekEnd(std::size_t  Offset ) noexcept
    {
        assert(m_pInstance);
        return m_pInstance->Seek( device::SKM_END, Offset );
    }

    //------------------------------------------------------------------------------
    inline
    err stream::SeekCurrent(std::size_t Offset ) noexcept
    {
        assert(m_pInstance);
        return m_pInstance->Seek( device::SKM_CURENT, Offset );
    }

    //------------------------------------------------------------------------------
    inline
    err stream::Tell(std::size_t& Pos ) noexcept
    {
        assert(m_pInstance);
        return m_pInstance->Tell(Pos);
    }

    //------------------------------------------------------------------------------
    inline
    bool stream::isEOF(void) noexcept
    {
        assert(m_pInstance);
        return m_pInstance->isEOF();
    }

    //------------------------------------------------------------------------------
    inline
    err stream::getC( int& C ) noexcept
    {
        assert(m_pInstance);
        std::uint8_t x;
        if ( auto Err = ReadRaw(std::span<std::byte>{ reinterpret_cast<std::byte*>(&x), sizeof(x)}); Err ) 
            return Err;

        C = x;
        return {};
    }

    //------------------------------------------------------------------------------
    inline
    err stream::putC( int aC, int Count, bool bUpdatePos ) noexcept
    {
        assert( aC >= 0 && aC <= 0xff);
        assert(m_pInstance);

        std::size_t     iPos{ 0 };
        std::uint8_t    C   = static_cast<std::uint8_t>(aC);

        if (Count == 0) return {};
        if (bUpdatePos == false) 
        {
            if ( auto Err = Tell(iPos); Err ) 
                return Err;
        }

        for( int i = 0; i < Count; i++ )
        {
            if ( auto Err = WriteRaw({ reinterpret_cast<const std::byte*>(&C), sizeof(C) }); Err )
                return Err;
        }

        if (bUpdatePos == false)
        {
            if (auto Err = SeekOrigin(iPos); Err) 
                return Err;
        }

        return {};
    }

    //------------------------------------------------------------------------------
    inline
    err stream::AlignPutC( int C, int Count, int Aligment, bool bUpdatePos ) noexcept
    {
        assert(C >= 0 && C <= 0xff);
        assert(m_pInstance);

        std::size_t Pos = 0;

        // First solve the alignment issue
        if (auto Err = Tell(Pos); Err) 
            return Err;

        const int PutCount = static_cast<int>(details::Align(Count + Pos, Aligment) - Pos);

        // Put all the necessary characters
        return putC(C, PutCount, bUpdatePos);
    }

    //------------------------------------------------------------------------------
    inline
    err stream::getFileLength( std::size_t& Length ) noexcept
    {
        assert(m_pInstance);
        return m_pInstance->Length(Length);
    }

    //------------------------------------------------------------------------------
    inline
    err stream::ToFile( stream& File ) noexcept
    {
        // Seek at the begging of the file
        if( auto Err = SeekOrigin( 0 ); Err ) 
            return Err;

        std::size_t                       i;
        std::array<std::byte, 2 * 256>    Buffer;
        std::size_t                       Length;
        
        if( auto Err = getFileLength(Length); Err ) 
            return Err;

        if (Length > Buffer.size() )
        {
            Length -= Buffer.size();
            for (i = 0; i < Length; i += 256 )
            {
                if( auto Err = ReadRaw(Buffer); Err ) 
                    return Err;

                if( auto Err = File.WriteRaw(Buffer) ) 
                    return Err;
            }
            Length += Buffer.size();
        }

        // Write trailing bytes
        auto S = Length - i;
        if (S)
        {
            auto FinalView = std::span{ Buffer.data(), Length - i };
            if( auto Err = ReadRaw(FinalView); Err) 
                return Err;

            return File.WriteRaw(FinalView);
        }

        return {};
    }

    //------------------------------------------------------------------------------
    inline
    err stream::ToMemory( std::span<std::byte> View ) noexcept
    {
        err Error;

        // Seek at the begging of the file
        if ( auto Err = SeekOrigin(0); Err ) 
            return Err;

        std::size_t Length;
        if ( auto Err = getFileLength(Length); Err )
            return Err;

        if ( Length < View.size()) 
            return err::create_f<"Buffer is too small">();

        return ReadRaw({ View.data(), Length });
    }

    //------------------------------------------------------------------------------

    inline
    err stream::ReadString( std::string& Buffer) noexcept
    {
        int c;

        while (true) 
        {
            if (auto err = getC(c); err)
                return err;

            if (c == 0) // Null terminator
                break;

            Buffer.push_back(static_cast<char>(c));
        }

        return {};
    }

    //------------------------------------------------------------------------------

    err stream::WriteString( const std::string_view String ) noexcept
    {
        if ( auto Err = m_pInstance->Write( { reinterpret_cast<const std::byte*>(String.data()), String.length() } ); Err )
            return Err;

        // if we are doing binary we better know where the string end...
        if (m_AccessType.m_Text == 0 ) 
        {
            if ( auto Err = putC(0); Err )
                return Err;
        }

        return {};
    }

    //------------------------------------------------------------------------------

    err stream::WriteString(const std::wstring_view String) noexcept
    {
        if (auto Err = m_pInstance->Write({ reinterpret_cast<const std::byte*>(String.data()), String.length()*2 }); Err)
            return Err;

        // if we are doing binary we better know where the string end...
        if (m_AccessType.m_Text == 0)
        {
            wchar_t C = 0;
            if (auto Err = Write(C); Err)
                return Err;
        }

        return {};
    }

    //------------------------------------------------------------------------------

    template<typename... T_ARGS> inline
    err stream::Printf( const char* pFormatStr, const T_ARGS& ... Args ) noexcept
    {
        constexpr auto size = 512;
        auto Scratch = std::make_unique<char[]>(size);
        std::size_t c = _sprintf(Scratch.get(), size, pFormatStr, Args...);
        return WriteSpan( {reinterpret_cast<const std::byte*>(Scratch.get()), c});
    }

    //------------------------------------------------------------------------------

    template<typename... T_ARGS> inline
    err stream::wPrintf(const wchar_t* pFormatStr, const T_ARGS& ... Args) noexcept
    {
        constexpr auto size = 512;
        auto Scratch = std::make_unique<wchar_t[]>(size);
        std::size_t c = _wsprintf(Scratch.get(), size, pFormatStr, Args...);
        return WriteSpan({ reinterpret_cast<const std::byte*>(Scratch.get()), c*2 });
    }
}
