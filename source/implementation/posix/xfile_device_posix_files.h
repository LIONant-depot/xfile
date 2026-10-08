// POSIX (Linux) file device for xfile - the counterpart of implementation/windows/xfile_device_window_files.h.
//
// Paths on POSIX have no drive letter, so this device registers the pseudo device name "/:" and
// xfile.cpp maps every absolute POSIX path (leading '/') to it (see SetTheFinalPathAndFindDevice).
// I/O is synchronous: an "async" ('@') open is honored by completing the request immediately, so
// Synchronize() always reports completion - the semantics callers rely on (data ready after Synchronize).
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <filesystem>

namespace xfile::driver::posix
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
            int                         m_FD                { -1 };
            access_types                m_AccessTypes       {};
            std::int16_t                m_iNext             {};
            bool                        m_bLastReadHitEOF   { false };
            std::wstring                m_LastError         {};

            void clear()
            {
                m_FD              = -1;
                m_AccessTypes     = {};
                m_bLastReadHitEOF = false;
                m_LastError       = {};
            }

            void CollectErrorAsString(int Err) noexcept
            {
                const char* p = std::strerror(Err);
                m_LastError.assign(p, p + std::strlen(p));
            }

            xerr open(std::wstring_view FileName, access_types AccessTypes) noexcept override
            {
                assert(FileName.empty() == false);

                int Flags = O_CLOEXEC;
                if (AccessTypes.m_bCreate)      Flags |= O_RDWR | O_CREAT | O_TRUNC;   // CREATE_ALWAYS, read+write
                else if (AccessTypes.m_bWrite)  Flags |= O_RDWR;                       // OPEN_EXISTING, read+write
                else                            Flags |= O_RDONLY;                     // OPEN_EXISTING, read only

                const std::string Path = std::filesystem::path(std::wstring(FileName)).string();
                const int FD = ::open(Path.c_str(), Flags, 0666);
                if (FD < 0)
                {
                    const int Err = errno;
                    CollectErrorAsString(Err);
                    switch (Err)
                    {
                    case ENOENT:  return xerr::create<state::OPENING_FILE, "The system cannot find the file specified.">();
                    case EACCES:  return xerr::create<state::OPENING_FILE, "Access is denied.">();
                    case ENOTDIR: return xerr::create<state::OPENING_FILE, "The system cannot find the path specified.">();
                    default:      return xerr::create<state::OPENING_FILE, "Unknown error.">();
                    }
                }

                m_FD              = FD;
                m_AccessTypes     = AccessTypes;
                m_bLastReadHitEOF = false;
                return {};
            }

            void close(void) noexcept override
            {
                if (m_FD >= 0 && ::close(m_FD) != 0) CollectErrorAsString(errno);
                m_FD = -1;
            }

            xerr Read(std::span<std::byte> View) noexcept override
            {
                std::size_t Done = 0;
                while (Done < View.size())
                {
                    const ssize_t n = ::read(m_FD, View.data() + Done, View.size() - Done);
                    if (n < 0)
                    {
                        if (errno == EINTR) continue;
                        CollectErrorAsString(errno);
                        return xerr::create_f<state, "Error while reading">();
                    }
                    if (n == 0) break;
                    Done += static_cast<std::size_t>(n);
                }
                // Same as ReadFile: nothing at all left to read is an EOF error (and what isEOF reports), a short read is not.
                m_bLastReadHitEOF = (Done == 0 && View.size() > 0);
                if (m_bLastReadHitEOF)
                    return xerr::create<state::UNEXPECTED_EOF, "Unexpected End Of File while reading">();
                return {};
            }

            xerr Write(const std::span<const std::byte> View) noexcept override
            {
                std::size_t Done = 0;
                while (Done < View.size())
                {
                    const ssize_t n = ::write(m_FD, View.data() + Done, View.size() - Done);
                    if (n < 0)
                    {
                        if (errno == EINTR) continue;
                        CollectErrorAsString(errno);
                        return xerr::create_f<state, "Error while writing">();
                    }
                    Done += static_cast<std::size_t>(n);
                }
                return {};
            }

            xerr Seek(seek_mode Mode, std::size_t Pos) noexcept override
            {
                int Whence = SEEK_SET;
                switch (Mode) //-V719
                {
                case SKM_CURENT: Whence = SEEK_CUR; break;
                case SKM_END:    Whence = SEEK_END; break;
                default:         assert(Mode == SKM_ORIGIN); break;
                }
                if (::lseek(m_FD, static_cast<off_t>(Pos), Whence) == static_cast<off_t>(-1))
                {
                    CollectErrorAsString(errno);
                    return xerr::create_f<state, "Fail to seek">();
                }
                m_bLastReadHitEOF = false;
                return {};
            }

            xerr Tell(std::size_t& Pos) noexcept override
            {
                const off_t P = ::lseek(m_FD, 0, SEEK_CUR);
                if (P == static_cast<off_t>(-1))
                {
                    CollectErrorAsString(errno);
                    return xerr::create_f<state, "Error while Telling">();
                }
                Pos = static_cast<std::size_t>(P);
                return {};
            }

            void Flush(void) noexcept override
            {
                // Writes go straight to the kernel; nothing is buffered here.
            }

            xerr Length(std::size_t& Length) noexcept override
            {
                struct stat St;
                if (::fstat(m_FD, &St) != 0)
                {
                    CollectErrorAsString(errno);
                    return xerr::create_f<state, "Error while getting the length">();
                }
                Length = static_cast<std::size_t>(St.st_size);
                return {};
            }

            // As the Windows device: end of file is a read that found nothing left, not being positioned at the end. A stream
            // that read its last block exactly up to the end is not at EOF yet (xserializer's LoadObject Synchronize()s after
            // that read and asserted on every resource whose last block ends the file).
            bool isEOF(void) noexcept override
            {
                return m_bLastReadHitEOF;
            }

            xerr Synchronize(bool) noexcept override
            {
                return {}; // every request already completed synchronously
            }

            void AsyncAbort(void) noexcept override
            {
            }
        };

        std::array<small_file, 128>     m_FileHPool;
        std::atomic<next>               m_iEmptyHead = { {0,0} };

        device()
        {
            for (std::size_t i = 0; i < m_FileHPool.size(); ++i)
                m_FileHPool[i].m_iNext = static_cast<std::int16_t>(i + 1);
            m_FileHPool[m_FileHPool.size() - 1].m_iNext = static_cast<std::int16_t>(-1);
        }

        void Init(const void*) noexcept override {}

        void Kill(void) noexcept override
        {
            for (auto& E : m_FileHPool)
                if (E.m_iNext == -2) { assert(false); }
        }

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
                    m_FileHPool[Local.m_iNext].m_iNext = -2;
                    break;
                }
            } while (true);
            return &m_FileHPool[Local.m_iNext];
        }

        void destroyInstance(instance& Instance) noexcept override
        {
            auto&             SmallFile = *reinterpret_cast<small_file*>(&Instance);
            const std::size_t Index     = static_cast<std::size_t>(&SmallFile - m_FileHPool.data());
            assert(Index < m_FileHPool.size());
            assert(SmallFile.m_iNext == -2);
            SmallFile.clear();
            auto Local = m_iEmptyHead.load(std::memory_order_relaxed);
            do
            {
                SmallFile.m_iNext = Local.m_iNext;
                auto NewValue = Local;
                NewValue.m_iNext = static_cast<std::int16_t>(Index);
                NewValue.m_Counter++;
                if (m_iEmptyHead.compare_exchange_weak(Local, NewValue, std::memory_order_release))
                    break;
            } while (true);
        }
    };

    static xfile::driver::posix::device  s_PosixDevice;
    static device::registration          s_PosixDeviceRegistration("PosixDevice", s_PosixDevice, "/:");
}