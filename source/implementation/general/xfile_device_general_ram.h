namespace xfile::driver::ram
{
    //==============================================================================
    //  MEMORY FILE CLASS
    //==============================================================================
    //  memfile 
    //      memfile is a class that contains the interface to access the memory files.
    //      currently, it is implemented as an array of memblock's of the size
    //      block_size_v
    //==============================================================================
    struct memfile : xfile::device::instance
    {
        //------------------------------------------------------------------------------

        xerr open (std::wstring_view FileName, xfile::device::access_types AccessTypes)   noexcept override
        {
            // File already open
            assert(m_EOF == 0);
            return {};
        }

        //------------------------------------------------------------------------------

        void close (void) noexcept override
        {
            
        }

        //------------------------------------------------------------------------------

        xerr Read (std::span<std::byte> View) noexcept override
        {
            if (m_SeekPosition >= m_EOF)
                return xerr::create<state::UNEXPECTED_EOF, "Unexpected End of File">();

            auto currentBlockIndex  = m_SeekPosition / block_size_v;
            auto currentBlockOffset = m_SeekPosition % block_size_v;
            std::uint64_t bufferOffset = 0;

            assert(currentBlockIndex >= 0 && currentBlockIndex < m_lBlock.size());

            while (bufferOffset < View.size())
            {
                // Copy data from blocks.
                View[bufferOffset] = (*m_lBlock[currentBlockIndex])[currentBlockOffset];
                currentBlockOffset++;
                bufferOffset++;
                m_SeekPosition++;
                if (currentBlockOffset >= block_size_v)
                {
                    // Since we've completed the current block, increment to next block.
                    currentBlockIndex++;
                    currentBlockOffset = 0;

                    if (currentBlockIndex >= m_lBlock.size())
                        return xerr::create_f<state,"Fail to read all the bytes from the ram drive">();
                }
            }

            return {};
        }

        //------------------------------------------------------------------------------

        xerr Write (const std::span<const std::byte> View) noexcept override
        {
            // Check current position and size of data being added.
            const auto NewDataPosition = m_SeekPosition + View.size();
            if (m_EOF < static_cast<std::int64_t>(NewDataPosition)) m_EOF = NewDataPosition;

            // If we need to allocate more memory for the blocks, then do so.
            if (m_EOF >= static_cast<std::int64_t>(m_lBlock.size() * block_size_v))
            {
                auto NumBlocksRequired = m_EOF / block_size_v + 1;
                NumBlocksRequired -= m_lBlock.size();

                // Allocate list of blocks
                for (int i = 0; i < NumBlocksRequired; i++)
                {
                    m_lBlock.push_back(std::unique_ptr<block>{ new block });
                }
            }

            auto currentBlockIndex = m_SeekPosition / block_size_v;
            auto currentBlockOffset = m_SeekPosition % block_size_v;
            std::uint64_t bufferOffset = 0;

            assert(currentBlockIndex >= 0 && currentBlockIndex < m_lBlock.size());

            while (bufferOffset < View.size())
            {
                // Copy data into blocks.
                (*m_lBlock[currentBlockIndex])[currentBlockOffset] = View[bufferOffset];

                bufferOffset++;
                currentBlockOffset++;
                m_SeekPosition++;

                if (currentBlockOffset >= block_size_v)
                {
                    // Since we've completed the current block, increment to next block.
                    currentBlockIndex++;
                    currentBlockOffset = 0;
                    assert(currentBlockIndex < m_lBlock.size());
                }
            }

            return {};
        }

        //------------------------------------------------------------------------------
        void        SeekOrigin      (std::size_t Offset)    noexcept { m_SeekPosition  = Offset;         assert(m_SeekPosition<=m_EOF && m_SeekPosition >= 0); }
        void        SeekCurrent     (std::size_t Offset)    noexcept { m_SeekPosition += Offset;         assert(m_SeekPosition<=m_EOF && m_SeekPosition >= 0); }
        void        SeekEnd         (std::size_t Offset)    noexcept { m_SeekPosition  = m_EOF - Offset; assert(m_SeekPosition<=m_EOF && m_SeekPosition >= 0); }

        //------------------------------------------------------------------------------

        xerr Seek(xfile::device::seek_mode Mode, std::size_t Pos) noexcept override
        {
            assert(Pos >= 0);
            switch (Mode)
            {
            case xfile::device::SKM_ORIGIN: SeekOrigin(Pos); break;
            case xfile::device::SKM_CURENT: SeekCurrent(Pos); break;
            case xfile::device::SKM_END:    SeekEnd(Pos); break;
            default: assert(0); break;
            }

            return {};

        }

        //------------------------------------------------------------------------------

        xerr Tell ( std::size_t& Pos) noexcept override
        {
            Pos = m_SeekPosition;
            return {};
        }

        //------------------------------------------------------------------------------

        void Flush ( void ) noexcept override
        {
            
        }

        //------------------------------------------------------------------------------

        xerr Length(std::size_t& L) noexcept override
        {
            L = m_EOF;
            return {};
        }

        //------------------------------------------------------------------------------

        bool isEOF ( void ) noexcept override
        {
            return m_SeekPosition > m_EOF;
        }

        //------------------------------------------------------------------------------

        xerr Synchronize(bool bBlock) noexcept override
        {
            return {};
        }

        //------------------------------------------------------------------------------

        void AsyncAbort (void) noexcept override
        {
            
        }

        //------------------------------------------------------------------------------

        void clear() noexcept
        {
            m_lBlock.clear();
            m_SeekPosition  = 0;
            m_EOF           = 0;
            m_iNext         = {};
        }

        //------------------------------------------------------------------------------

        constexpr static std::size_t block_size_v = 1024 * 10;
        using block = std::array< std::byte, block_size_v >;

        std::vector<std::unique_ptr<block>>     m_lBlock        {};
        std::int64_t                            m_SeekPosition  { 0 };
        std::int64_t                            m_EOF           { 0 };
        std::int16_t                            m_iNext         {};
    };

    //------------------------------------------------------------------------------

    struct next
    {
        std::int16_t    m_iNext;
        std::uint16_t   m_Counter;
    };

    //------------------------------------------------------------------------------

    struct device final : xfile::device
    {
        std::array<memfile, 128>    m_FileHPool;
        std::atomic<next>           m_iEmptyHead = { {0,0} };

        //------------------------------------------------------------------------------

        inline device(void) noexcept
        {
            // Initialize the pool of handles
            for (std::size_t i = 0; i < m_FileHPool.size(); ++i)
            {
                m_FileHPool[i].m_iNext = static_cast<std::int16_t>(i + 1);
            }
            m_FileHPool[m_FileHPool.size() - 1].m_iNext = static_cast<std::int16_t>(-1);
        }

        //------------------------------------------------------------------------------

        virtual void Init(const void*) noexcept
        {
        }

        //------------------------------------------------------------------------------

        virtual void Kill(void) noexcept
        {
            for (auto& E : m_FileHPool)
            {
                if (E.m_iNext == -2)
                {
                    // This should have been freed
                    assert(false);
                }
            }
        }

        //------------------------------------------------------------------------------

        virtual instance* createInstance(void) noexcept
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

        //------------------------------------------------------------------------------

        virtual void destroyInstance(instance& Instance) noexcept
        {
            auto&               SmallFile   = *reinterpret_cast<memfile*>(&Instance);
            const std::size_t   Index       = static_cast<std::size_t>(&SmallFile - m_FileHPool.data());
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
    static xfile::driver::ram::device   s_RamDevice;
    static device::registration         s_RamDeviceRegistration("RamDevice", s_RamDevice, "ram:");
}