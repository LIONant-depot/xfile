#include "xfile.h"
#include <cassert>
#include <filesystem>
#include <cwctype>


#include "implementation/windows/xfile_device_window_files.h"
#include "implementation/general/xfile_device_general_ram.h"


static std::wstring TempPath;

namespace xfile
{
    //------------------------------------------------------------------------------

    const std::wstring_view getTempPath(void) noexcept
    {
        if ( TempPath.empty() ) 
        {
            TempPath = std::filesystem::temp_directory_path();
        }

        return TempPath;
    }

    //------------------------------------------------------------------------------

    std::wstring_view fromPathGetDeviceName(std::wstring_view Path) noexcept
    {
        if (Path.empty()) return {};

        // Does it have a device?
        for( std::size_t i = 0, end = Path.length(); (i < end); i++)
        {
            if (Path[i] == L':')
            {
                return { Path.data(), i+1 };
            }
        }

        // If it does not have a device assign in the path we will assume it is the working path...
        return fromPathGetDeviceName( std::filesystem::current_path().c_str() );
    }

    //------------------------------------------------------------------------------

    std::wstring to_lower(std::wstring_view input)
    {
        std::wstring result;
        result.reserve(input.size());

        for (wchar_t ch : input) 
        {
            result.push_back(std::towlower(ch));
        }

        return result;
    }

    //------------------------------------------------------------------------------

    static device::registration* SetTheFinalPathAndFindDevice( std::wstring& FinalPath, std::wstring_view Path) noexcept
    {
        //
        // Make sure to clear out current path
        //
        FinalPath.clear();

        //
        // Set the final name and collect the device name is lower case
        //
        std::wstring DeviceNameLower;
        {
            std::wstring_view DeviceName;
            for (std::size_t i = 0, end = Path.length(); (i < end); i++)
            {
                if (Path[i] == L':')
                {
                    DeviceName = std::wstring_view{ Path.data(), i+1 };
                    break;
                }
            }

            if (DeviceName.empty() == false )
            {
                DeviceNameLower = to_lower(DeviceName);
            }

            // If the user did not enter any device name we will assume it is using the current_path...
            if (DeviceName.empty())
            {
                FinalPath = std::format(L"{}//{}", std::filesystem::current_path().c_str(), Path);
                DeviceNameLower = to_lower(fromPathGetDeviceName(FinalPath));
            }
            // If the user is using the temp drive we must actually use the right path
            else if (DeviceNameLower.length() == sizeof("temp") && std::wcsncmp(DeviceNameLower.data(), L"temp:", sizeof("temp:") - 1) == 0)
            {
                FinalPath = std::format(L"{}{}", getTempPath(), Path.substr(sizeof("temp:")));
                DeviceNameLower = to_lower(fromPathGetDeviceName(FinalPath));
            }
            else
            {
                FinalPath = Path;
            }
        }

        //
        // Force the final path to terminate with a null character... maximizes compatibility with drivers
        //
        (void)FinalPath.c_str();


        //
        // At this point we should know which device we are dealing with...
        //
        assert(DeviceNameLower.empty() == false);

        for ( auto pDevice = device::registration::s_pHead; pDevice; pDevice = pDevice->m_pNext )
        {
            const char* pName = pDevice->m_pNames;

            for (std::size_t i = 0, end = DeviceNameLower.length(); (i != end) && *pName; i++, pName++)
            {
                if (DeviceNameLower[i] == ':' && *pName == ':')
                    return pDevice;

                if ( *pName != DeviceNameLower[i] )
                {
                    // Skip character and see if it has more devices mapped
                    while (*pName != ':' && *pName) pName++;

                    // Check to see if a has more devices
                    if (*pName == ':')
                    {
                        if (pName[1])
                        {
                            i = -1;
                            continue;
                        }
                        break;
                    }
                }
            }
        }

        // The device must not be implemented... or the user type the wrong device?
        return nullptr;
    }

    //------------------------------------------------------------------------------

    xerr stream::open( const std::wstring_view Path, const char* pMode) noexcept
    {
        assert(pMode);

        // We cant open a new file in the middle of using another
        assert( m_pInstance == nullptr );

        //
        // Get the registration device and set the FilePath
        //
        m_pDeviceReg = SetTheFinalPathAndFindDevice(m_FilePath, Path);

        // Make sure that we got a device
        if (m_pDeviceReg == nullptr) 
            return xerr::create<state::DEVICE_FAILURE, "Unable to find requested device">();

        //
        // Okay now lets make sure that the mode is correct
        //
        bool bSeekToEnd = false;

        // start from zero...
        m_AccessType.m_Value = 0;

        for( int i = 0; pMode[i]; i++ )
        {
            switch (pMode[i])
            {
            case 'a':   m_AccessType.m_bRead      = m_AccessType.m_bWrite = true;   bSeekToEnd = true;      break;
            case 'r':   m_AccessType.m_bRead      = true;                                                   break;
            case '+':   m_AccessType.m_bWrite     = true;                                                   break;
            case 'w':   m_AccessType.m_bRead      = m_AccessType.m_bWrite = m_AccessType.m_bCreate = true;  break;
            case 'c':   m_AccessType.m_bCompress  = true;                                                   break;
            case '@':   m_AccessType.m_bASync     = true;                                                   break;
            case 't':   m_AccessType.m_Text       = 1;                                                      break;
            case 'T':   m_AccessType.m_Text       = 2;                                                      break;
            case 'b':   m_AccessType.m_Text       = 0;                                                      break;
            default:
                // "Don't understand this[%c] access mode while opening file (%s)", pMode[i], (const char*)Path)
                assert(false);
            }
        }

        //
        // Ok we are ready to make things happen...
        //
        if ( m_pDeviceReg->s_nHaveUsed == 0 )
        {
            ++m_pDeviceReg->s_nHaveUsed;
            m_pDeviceReg->m_pDevice->Init(nullptr);
        }

        // Ok let the system know that we have one guy using it
        ++m_pDeviceReg->s_nInUse;

        // We wait to sync with everyone else in case there is a race condition here
        while( m_pDeviceReg->s_nHaveUsed.load() == 0 ){}

        // Ok let us create our instance from the device
        m_pInstance = m_pDeviceReg->m_pDevice->createInstance();

        // next thing is to open the file using the device
        // Note this hold initialization could be VERY WORNG as opening the file 
        // may the one of the slowest part of the file access as the DVD may need to seek
        // to it. This ideally should happen Async when an Async mode is requested.
        // May need to review this a bit more careful later.
        if (auto Err = m_pInstance->open(m_FilePath, m_AccessType); Err)
        {
            m_pDeviceReg->m_pDevice->destroyInstance(*m_pInstance);
            m_pInstance = nullptr;
            --m_pDeviceReg->s_nInUse;
            return Err;
        }

        if( m_AccessType.m_bCreate == false )
        {
            if (bSeekToEnd)
            {
                if( auto Err = m_pInstance->Seek( device::SKM_END, 0 ); Err )
                {
                    m_pDeviceReg->m_pDevice->destroyInstance(*m_pInstance);
                    m_pInstance = nullptr;
                    --m_pDeviceReg->s_nInUse;
                    return xerr::create<state::INCOMPLETE, "Able to open the file but failed to seek at the end of the file">();
                }
            }
        }

        return {};
    }

    //------------------------------------------------------------------------------

    void stream::close(void) noexcept
    {
        if (m_pInstance)
        {
            m_pInstance->close();
            --m_pDeviceReg->s_nInUse;
            m_pDeviceReg->m_pDevice->destroyInstance(*m_pInstance);
        }

        //
        // Done with the file
        //
        m_pInstance = nullptr;
        m_pDeviceReg = nullptr;
        m_AccessType.m_Value = 0;
        m_FilePath.clear();
    }

    //------------------------------------------------------------------------------

    xerr stream::ReadRaw(std::span<std::byte> View) noexcept
    {
        assert(m_pInstance);
        assert(View.empty() == false);

        // Read data if we have an error report it to the user
        if (auto Err = m_pInstance->Read(View); Err )
            return Err;

        // If it is text mode try finding '\r\n' to remove the '\r'
        if (m_AccessType.m_Text)
        {
            if (m_AccessType.m_Text==1)
            {
                char* pCharSrc = reinterpret_cast<char*>(View.data());
                char* pCharDst = reinterpret_cast<char*>(View.data());

                for (std::size_t i = 0; i < View.size(); i++)
                {
                    *pCharDst = *pCharSrc;
                    pCharSrc++;
                    if (!(*pCharSrc == '\n' && *(pCharSrc - 1) == '\r'))
                    {
                        pCharDst++;
                    }
                }

                // Read any additional data that we may need
                if (pCharSrc != pCharDst)
                {
                    const auto Delta = static_cast<std::size_t>(pCharSrc - pCharDst);

                    // Recurse
                    return ReadRaw({ (std::byte*)&View[View.size() - Delta], Delta });
                }

                // Sugar a bad case here. We need to read one more character to know what to do.
                if (*--pCharDst == '\r')
                {
                    char C;

                    if ( auto Err = m_pInstance->Read( { (std::byte*)&C, 1 }); Err )
                        return Err;

                    if (C == '\n')
                    {
                        *pCharDst = '\n';
                    }
                    else
                    {
                        // Upss the next character didnt match the sequence
                        // lets rewind one
                        if (auto Err = m_pInstance->Seek(device::SKM_CURENT, -1 ); Err )
                            return Err;
                    }
                }
            }
            else if (m_AccessType.m_Text == 2)
            {
                if (( View.size()&1) == 0 )
                {
                    return xerr::create_f<state,"The text buffer you are trying to read is not multiple of 2... For wide character it needs to be multiple of 2">();
                }
                auto     wView    = std::span(reinterpret_cast<wchar_t*>(View.data()), View.size()/2);
                wchar_t* pCharSrc = wView.data();
                wchar_t* pCharDst = wView.data();

                for (std::size_t i = 0; i < wView.size(); i++)
                {
                    *pCharDst = *pCharSrc;
                    pCharSrc++;
                    if (!(*pCharSrc == '\n' && *(pCharSrc - 1) == '\r'))
                    {
                        pCharDst++;
                    }
                }

                // Read any additional data that we may need
                if (pCharSrc != pCharDst)
                {
                    const auto Delta = static_cast<std::size_t>(pCharSrc - pCharDst);

                    // Recurse
                    return ReadRaw({ (std::byte*)&wView[wView.size() - Delta], Delta*2 });
                }

                // Sugar a bad case here. We need to read one more character to know what to do.
                if (*--pCharDst == L'\r')
                {
                    wchar_t C;

                    if (auto Err = m_pInstance->Read({ (std::byte*)&C, 2 }); Err)
                        return Err;

                    if (C == L'\n')
                    {
                        *pCharDst = L'\n';
                    }
                    else
                    {
                        // Upss the next character didnt match the sequence
                        // lets rewind one
                        if (auto Err = m_pInstance->Seek(device::SKM_CURENT, -2); Err)
                            return Err;
                    }
                }
            }
        }

        return {};
    }

    //------------------------------------------------------------------------------

    xerr stream::WriteRaw(std::span<const std::byte> View) noexcept
    {
        assert(m_pInstance);
        assert(View.empty() == false);

        // If it is text mode try finding '\n' and add a '\r' in front so that it puts in the file '\r\n'
        if (m_AccessType.m_Text)
        {
            if (m_AccessType.m_Text == 1)
            {
                auto* pFound = View.data();
                std::uint64_t   iLast = 0;
                std::uint64_t   i;

                for (i = 0; i < View.size(); i++)
                {
                    if (pFound[i] == std::byte{ '\n' })
                    {
                        constexpr static std::uint16_t Data = (std::uint16_t{ '\n' } << 8) | (std::uint16_t{ '\r' } << 0);

                        if (auto Err = m_pInstance->Write({ &pFound[iLast], i - iLast }); Err ) 
                            return Err;

                        if (auto Err = m_pInstance->Write({ reinterpret_cast<const std::byte*>(&Data), sizeof(Data)}); Err)
                            return Err;

                        // Update the base
                        iLast = i + 1;
                    }
                }

                // Write the remainder 
                if (iLast != i)
                {
                    if (auto Err = m_pInstance->Write({ &pFound[iLast], i - iLast }); Err ) 
                        return Err;
                }
            }
            else if (m_AccessType.m_Text == 2)
            {
                auto            wView  = std::span(reinterpret_cast<const wchar_t*>(View.data()), View.size()/2);
                auto*           pFound = wView.data();
                std::uint64_t   iLast = 0;
                std::uint64_t   i;

                for (i = 0; i < wView.size(); i++)
                {
                    if (pFound[i] == L'\n' )
                    {
                        constexpr static std::uint32_t Data = (std::uint16_t{ L'\n' } << 16) | (std::uint16_t{ L'\r' } << 0);

                        if (auto Err = m_pInstance->Write({ reinterpret_cast<const std::byte*>(&pFound[iLast]), (i - iLast) * 2}); Err)
                            return Err;

                        if (auto Err = m_pInstance->Write({ reinterpret_cast<const std::byte*>(&Data), sizeof(Data) }); Err )
                            return Err;

                        // Update the base
                        iLast = i + 1;
                    }
                }

                // Write the remainder 
                if (iLast != i)
                {
                    if (auto Err = m_pInstance->Write({ reinterpret_cast<const std::byte*>(&pFound[iLast]), (i - iLast)*2 }); Err )
                        return Err;
                }
            }
        }
        else
        {
            if (auto Err = m_pInstance->Write(View); Err ) 
                return Err;
        }

        if (m_AccessType.m_bForceFlush)
        {
            m_pInstance->Flush();
        }

        return {};
    }

}