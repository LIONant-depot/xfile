namespace xfile::driver::buffer
{
    //==============================================================================
    // The buffer device ("buffer:<address hex>,<size hex>", made by xfile::BufferPath): a read-only file over memory the caller owns
    // (it must outlive the stream). For data the caller read by its own means - a whole file at full disk speed - parsed by code
    // written for streams (xserializer).
    //==============================================================================
    struct view final : xfile::device::instance
    {
        const std::byte*    m_pData = nullptr;
        std::size_t         m_Size  = 0;
        std::size_t         m_Pos   = 0;
        bool                m_bEOF  = false;    // a read hit the end (as the file devices: reaching it is not yet the end of file)

        xerr open(std::wstring_view FileName, xfile::device::access_types Flags) noexcept override
        {
            if (Flags.m_bWrite || Flags.m_bCreate) return xerr::create_f<state, "The buffer device is read only">();
            const auto Colon = FileName.find(L':'), Comma = FileName.find(L',');
            if (Colon == std::wstring_view::npos || Comma == std::wstring_view::npos || Comma < Colon) return xerr::create_f<state, "A buffer path is buffer:<address hex>,<size hex>">();
            m_pData = reinterpret_cast<const std::byte*>(static_cast<std::uintptr_t>(std::wcstoull(std::wstring(FileName.substr(Colon + 1, Comma - Colon - 1)).c_str(), nullptr, 16)));
            m_Size  = static_cast<std::size_t>(std::wcstoull(std::wstring(FileName.substr(Comma + 1)).c_str(), nullptr, 16));
            m_Pos   = 0; m_bEOF = false;
            if (m_pData == nullptr && m_Size) return xerr::create_f<state, "A buffer path with no address">();
            return {};
        }

        void close(void) noexcept override {}

        // as the file devices: a read that crosses the end gets what there is (the position moves by the whole request); one that starts
        // at or past the end fails (xserializer reads a pack's last block by its whole size)
        xerr Read(std::span<std::byte> View) noexcept override
        {
            if (View.empty()) return {};
            if (m_Pos >= m_Size) { m_bEOF = true; return xerr::create<state::UNEXPECTED_EOF, "Unexpected End of File">(); }
            std::memcpy(View.data(), m_pData + m_Pos, std::min(View.size(), m_Size - m_Pos));
            m_Pos += View.size();
            return {};
        }

        xerr Write(const std::span<const std::byte>) noexcept override { return xerr::create_f<state, "The buffer device is read only">(); }

        xerr Seek(xfile::device::seek_mode Mode, std::size_t Pos) noexcept override
        {
            const std::size_t Base = Mode == xfile::device::SKM_ORIGIN ? 0 : Mode == xfile::device::SKM_CURENT ? m_Pos : m_Size;
            if (Base + Pos > m_Size) return xerr::create_f<state, "Seek past the end of a buffer">();
            m_Pos = Base + Pos; m_bEOF = false;
            return {};
        }

        xerr Tell(std::size_t& Pos) noexcept override       { Pos = m_Pos; return {}; }
        void Flush(void) noexcept override                  {}
        xerr Length(std::size_t& L) noexcept override       { L = m_Size; return {}; }
        bool isEOF(void) noexcept override                  { return m_bEOF; }
        xerr Synchronize(bool) noexcept override            { return {}; }
        void AsyncAbort(void) noexcept override             {}
        const std::byte* getMemory(void) noexcept override  { return m_pData; }
    };

    struct device final : xfile::device
    {
        void        Init            (const void*) noexcept override {}
        void        Kill            (void) noexcept override {}
        instance*   createInstance  (void) noexcept override { return new view; }
        void        destroyInstance (instance& Instance) noexcept override { delete static_cast<view*>(&Instance); }
    };

    static xfile::driver::buffer::device    s_BufferDevice;
    static device::registration             s_BufferDeviceRegistration("BufferDevice", s_BufferDevice, "buffer:");
}

namespace xfile
{
    std::wstring BufferPath(std::span<const std::byte> Data) noexcept
    {
        return std::format(L"buffer:{:X},{:X}", reinterpret_cast<std::uintptr_t>(Data.data()), Data.size());
    }
}
