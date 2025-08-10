
#define NOMINMAX
#include "windows.h"

namespace xfile::driver::windows
{
    struct device final : public xfile::device
    {
        struct next
        {
            std::int16_t    m_iNext;
            std::uint16_t   m_Counter;
        };

        struct alignas(std::atomic<void*>) small_file : device::instance
        {
            HANDLE                      m_Handle            {};
            OVERLAPPED                  m_Overlapped        {};
            access_types                m_AccessTypes       {};
            std::int16_t                m_iNext             {};
            bool                        m_bIOPending        { false };
            std::wstring                m_LastError         {};

            void clear()
            {
                m_Handle        = {};
                m_Overlapped    = {};
                m_AccessTypes   = {};
                m_bIOPending    = false;
                m_LastError     = {};
            }

            //----------------------------------------------------------------------------------------

            void CollectErrorAsString(void) noexcept
            {
                std::array<WCHAR, 256> Buffer;
                FormatMessage(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                    NULL,
                    GetLastError(),
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
                    Buffer.data(),
                    static_cast<DWORD>( Buffer.size() ),
                    NULL);

                m_LastError = std::format(L"{}", Buffer.data());
            }

            //----------------------------------------------------------------------------------------

            xerr open(std::wstring_view FileName, access_types AccessTypes) noexcept override
            {
                std::uint32_t FileMode      = 0;
                std::uint32_t Disposition   = 0;
                std::uint32_t AttrFlags     = 0;
                std::uint32_t ShareType     = FILE_SHARE_READ;

                assert(FileName.empty() == false);

                FileMode = GENERIC_WRITE | GENERIC_READ;
                if (AccessTypes.m_bCreate)
                {
                    Disposition = CREATE_ALWAYS;
                }
                else
                {
                    if (AccessTypes.m_bWrite == false)
                    {
                        FileMode &= ~GENERIC_WRITE;
                    }

                    Disposition = OPEN_EXISTING;
                }

                if (AccessTypes.m_bASync)
                {
                    // FILE_FLAG_OVERLAPPED     -	This allows asynchronous I/O.
                    // FILE_FLAG_NO_BUFFERING   -	No cached asynchronous I/O.
                    AttrFlags = FILE_FLAG_OVERLAPPED;// | FILE_FLAG_NO_BUFFERING;
                }
                else
                {
                    AttrFlags = FILE_ATTRIBUTE_NORMAL;
                }

                // open the file (or create a new one)
                HANDLE Handle = CreateFile(FileName.data(), FileMode, ShareType, nullptr, Disposition, AttrFlags, nullptr);
                if (Handle == INVALID_HANDLE_VALUE)
                {
                    DWORD errorCode = GetLastError();
                    CollectErrorAsString();
                    switch (errorCode)
                    {
                    case ERROR_FILE_NOT_FOUND: return xerr::create<state::OPENING_FILE, "The system cannot find the file specified.">();
                    case ERROR_ACCESS_DENIED:  return xerr::create<state::OPENING_FILE, "Access is denied.">();
                    case ERROR_INVALID_HANDLE: return xerr::create<state::OPENING_FILE, "The handle is invalid.">();
                    case ERROR_PATH_NOT_FOUND: return xerr::create<state::OPENING_FILE, "The system cannot find the path specified.">();
                    default:                   return xerr::create<state::OPENING_FILE, "Unknown error.">();
                    }
                }

                //
                // Okay we are in business
                //
                m_Handle = Handle;
                m_AccessTypes = AccessTypes;
                std::memset(&m_Overlapped, 0, sizeof(m_Overlapped));

                if (AccessTypes.m_bASync)
                {
                    // Create an event to detect when an asynchronous operation is done.
                    // Please see documentation for further information.
                    //HANDLE const Event = CreateEvent( NULL, TRUE, FALSE, NULL );
                    //ASSERT(Event != NULL);
                    //File.m_Overlapped.hEvent = Event;
                }

                // done
                return {};
            }

            //----------------------------------------------------------------------------------------

            void close(void) noexcept override
            {
                //
                // Close the handle
                //
                if (!CloseHandle(m_Handle))
                {
                    DWORD errorCode = GetLastError();
                    CollectErrorAsString();
                    return;
                }
            }

            //----------------------------------------------------------------------------------------

            xerr Read(std::span<std::byte> View) noexcept override
            {
                assert(View.size() < std::numeric_limits<DWORD>::max() );

                const DWORD Count       = static_cast<DWORD>(View.size());
                DWORD       nBytesRead  = Count;
                BOOL        bResult     = ReadFile( m_Handle
                                                    , View.data()
                                                    , Count
                                                    , &nBytesRead
                                                    , &m_Overlapped);

                // Set the file pointer (We assume we didn't make any errors)
                //if( File.m_Flags&xfile_device_i::ACC_ASYNC) 
                {
                    m_Overlapped.Offset += Count;
                }

                if (!bResult)
                {
                    DWORD dwError = GetLastError();

                    // deal with the error code 
                    switch (dwError)
                    {
                    case ERROR_HANDLE_EOF:
                    {
                        // we have reached the end of the file 
                        // during the call to ReadFile 
                        CollectErrorAsString();

                        // code to handle that 
                        return xerr::create<state::UNEXPECTED_EOF, "Unexpected End Of File while reading">();
                    }

                    case ERROR_IO_PENDING:
                    {
                        m_bIOPending = true;
                        return xerr::create<state::INCOMPLETE, "Still reading" >();
                    }

                    default:
                    {
                        CollectErrorAsString();
                        return xerr::create_f<state,"Error while reading">();
                    }
                    }
                }

                // Not problems
                return {};
            }

            //----------------------------------------------------------------------------------------

            xerr Write(const std::span<const std::byte> View) noexcept override
            {
                xerr  Error;
                assert(View.size() < std::numeric_limits<DWORD>::max());

                const DWORD Count = static_cast<DWORD>(View.size());
                DWORD       nBytesWritten = Count;
                BOOL  bResult = WriteFile( m_Handle
                                            , View.data()
                                            , Count
                                            , &nBytesWritten
                                            , &m_Overlapped);

                // Set the file pointer (We assume we didnt make any errors)
                //if( File.m_Flags&xfile_device_i::ACC_ASYNC) 
                {
                    m_Overlapped.Offset += Count;
                }

                if (!bResult)
                {
                    DWORD dwError = GetLastError();

                    // deal with the error code 
                    switch (dwError)
                    {
                        case ERROR_HANDLE_EOF:
                        {
                            // we have reached the end of the file 
                            // during the call to ReadFile 

                            // code to handle that 
                            CollectErrorAsString();
                            return xerr::create<state::UNEXPECTED_EOF, "Unexpected end of file while writing">();
                        }

                        case ERROR_IO_PENDING:
                        {
                            m_bIOPending = true;
                            return xerr::create<state::INCOMPLETE, "Still writing" >();
                        }

                        default:
                        {
                            CollectErrorAsString();
                            return xerr::create_f<state,"Error while writing">();
                        }
                    }
                }

                return {};
            }

            //----------------------------------------------------------------------------------------

            xerr Seek(seek_mode Mode, std::size_t Pos) noexcept override
            {
                auto HardwareMode = [](seek_mode Mode)
                    {
                        switch (Mode) //-V719
                        {
                        case SKM_CURENT: return FILE_CURRENT;
                        case SKM_END:    return FILE_END;
                        }
                        assert(Mode == SKM_ORIGIN);
                        return FILE_BEGIN;
                    }(Mode);

                // We will make sure we are sync here
                // WARNING: Potential time wasted here
                if (m_AccessTypes.m_bASync)
                {
                    if ( auto Err = Synchronize(true); Err ) 
                        return Err;
                }

                // Seek!
                LARGE_INTEGER Position;
                LARGE_INTEGER NewFilePointer;

                Position.QuadPart = Pos;
                const DWORD Result = SetFilePointerEx(m_Handle, Position, &NewFilePointer, HardwareMode);
                if (!Result)
                {
                    CollectErrorAsString();
                    if (Result == INVALID_SET_FILE_POINTER) //-V547
                    {
                        // Failed to seek.
                        return xerr::create_f<state,"Fail to seek, INVALID_SET_FILE_POINTER">();
                    }

                    return xerr::create_f<state,"Fail to seek">();
                }

                // Set the position for async files
                m_Overlapped.Offset     = NewFilePointer.LowPart;
                m_Overlapped.OffsetHigh = NewFilePointer.HighPart;

                return {};
            }

            //----------------------------------------------------------------------------------------

            xerr Tell(std::size_t& Pos) noexcept override
            {
                LARGE_INTEGER   Position;
                LARGE_INTEGER   NewFilePointer;

                Position.LowPart = 0;
                Position.HighPart = 0;

                if (FALSE == SetFilePointerEx(m_Handle, Position, &NewFilePointer, FILE_CURRENT))
                {
                    CollectErrorAsString();
                    return xerr::create_f<state,"Error while Telling">();
                }

                Pos = NewFilePointer.QuadPart;
                return {};
            }

            //----------------------------------------------------------------------------------------

            void Flush ( void ) noexcept override
            {
                // We will make sure we are sync here. 
                // I dont know what else to do there is not a way to flush anything the in the API
                // WARNING: Potential time wasted here
                auto E = Synchronize(true);
            }

            //----------------------------------------------------------------------------------------

            xerr Length(std::size_t& Length) noexcept override
            {
                std::size_t Cursor;

                if (auto Err = Tell(Cursor); Err ) 
                    return Err;

                if (auto Err = Seek(SKM_END, 0 ); Err ) 
                    return Err;

                if ( auto Err = Tell(Length); Err ) 
                    return Err;

                if ( auto Err = Seek(SKM_ORIGIN, Cursor); Err ) 
                    return Err;

                return {};

            }

            //----------------------------------------------------------------------------------------

            bool isEOF(void) noexcept override
            {
                DWORD nBytesTransfer;
                BOOL  bResult = GetOverlappedResult(m_Handle, &m_Overlapped, &nBytesTransfer, false);

                if (!bResult)
                {
                    DWORD dwError = GetLastError();

                    // deal with the error code 
                    switch (dwError)
                    {
                    case ERROR_HANDLE_EOF:
                    {
                        // we have reached the end of the file 
                        // during the call to ReadFile 

                        // code to handle that 
                        return true;
                    }

                    case ERROR_IO_PENDING:
                    {
                        if (auto Err = Synchronize(true); Err)
                        {
                            if (Err.getState<state>() == state::INCOMPLETE) return false;
                            Err.clear();
                        }

                        return true;
                    }

                    default:
                        CollectErrorAsString();
                        assert(0);
                        break;
                    }
                }

                // Not sure about this yet
                return false;
            }

            //----------------------------------------------------------------------------------------

            xerr Synchronize(bool bBlock) noexcept override
            {
                if (m_bIOPending)
                {
                    if (HasOverlappedIoCompleted(&m_Overlapped))
                    {
                        m_bIOPending = false;
                        return {}; // xfile::sync_state::COMPLETED;
                    }
                }

                // Get the current status.
                DWORD nBytesTransfer = 0;
                BOOL  Result = GetOverlappedResult(m_Handle, &m_Overlapped, &nBytesTransfer, bBlock);

                // Has the asynchronous operation finished?
                if (Result != FALSE)
                {
                    // Clear the event's signal since it isn't automatically reset.
                    // VERIFY( ResetEvent( File.m_Overlapped.hEvent ) == TRUE );

                    // The operation is complete.
                    m_bIOPending = false;
                    return {}; // xfile::sync_state::COMPLETED;
                }

                //
                // Deal with errors
                //
                DWORD dwError = GetLastError();
                if (dwError == ERROR_HANDLE_EOF)
                {
                    // we have reached the end of
                    // the file during asynchronous
                    // operation
                    return xerr::create< state::UNEXPECTED_EOF, "Unexpected end of file">();
                }

                if (dwError == ERROR_IO_INCOMPLETE)
                {
                    m_bIOPending = true;
                    return xerr::create<state::INCOMPLETE, "Incomplete">();
                }

                if (dwError == ERROR_OPERATION_ABORTED)
                {
                    CollectErrorAsString();
                    return xerr::create_f<state,"Operation Aborted">();
                }

                // The result is FALSE and the error isn't ERROR_IO_INCOMPLETE, there's a real error!
                CollectErrorAsString();
                return xerr::create_f<state,"Unknown Error">();
            }

            //----------------------------------------------------------------------------------------

            void AsyncAbort(void) noexcept override
            {
                auto E = CancelIo(m_Handle);
                if (!E) CollectErrorAsString();
            }
        };

        std::array<small_file, 128>     m_FileHPool;
        std::atomic<next>               m_iEmptyHead = {{0,0}};

        device()
        {
            // Initialize the pool of handles
            for( std::size_t i=0; i< m_FileHPool.size(); ++i )
            {
                m_FileHPool[i].m_iNext = static_cast<std::int16_t>(i + 1);
            }
            m_FileHPool[m_FileHPool.size()-1].m_iNext = static_cast<std::int16_t>(-1);
        }

        //----------------------------------------------------------------------------------------

        void Init(const void*) noexcept override
        {
            // Nothing to do...
        }

        //----------------------------------------------------------------------------------------

        void Kill(void) noexcept override
        {
            for ( auto& E : m_FileHPool )
            {
                if (E.m_iNext == -2)
                {
                    // This should have been freed
                    assert(false);
                }
            }
        }

        //----------------------------------------------------------------------------------------

        instance* createInstance(void) noexcept override
        {
            auto Local = m_iEmptyHead.load(std::memory_order_relaxed);
            do
            {
                assert(Local.m_iNext >= 0);

                auto NewValue = Local;

                NewValue.m_iNext = m_FileHPool[Local.m_iNext].m_iNext;
                NewValue.m_Counter++;

                if (m_iEmptyHead.compare_exchange_weak(Local, NewValue, std::memory_order_release))
                {
                    // Let us mark this entry as is now ours!
                    m_FileHPool[Local.m_iNext].m_iNext = -2;
                    break;
                }

            } while (true);

            // Return the instance
            return &m_FileHPool[Local.m_iNext];
        }

        //----------------------------------------------------------------------------------------

        void destroyInstance(instance& Instance) noexcept override
        {
            auto&               SmallFile = *reinterpret_cast<small_file*>(&Instance);
            const std::size_t   Index     = static_cast<std::size_t>( &SmallFile - m_FileHPool.data());
            assert(Index < m_FileHPool.size());
            assert(SmallFile.m_iNext == -2);

            // OK we can officially free everything from our entry
            SmallFile.clear();

            //
            // Now we must insert it into the free list
            //
            auto Local = m_iEmptyHead.load(std::memory_order_relaxed);
            do
            {
                // Add the structure into the chain
                SmallFile.m_iNext = Local.m_iNext;

                auto NewValue = Local;
                NewValue.m_iNext = static_cast<std::int16_t>(Index);
                NewValue.m_Counter++;

                if (m_iEmptyHead.compare_exchange_weak(Local, NewValue, std::memory_order_release))
                    break;

            } while (true);
        }
    };

    //
    // Registration functions... here we create the device as well as we register with the file system
    //
    static xfile::driver::windows::device   s_WindowDevice;
    static device::registration             s_WindowsDeviceRegistration("WindowsDevice", s_WindowDevice, "a:b:c:d:e:f:g:h:i:j:k:l:m:n:o:p:q:r:s:t:u:v:w:x:y:z:");
}
