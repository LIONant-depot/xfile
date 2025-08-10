#ifndef XFILE_H
#define XFILE_H
#pragma once

#include <array>
#include <span>
#include <string>
#include <memory>

#include "source/xerr.h"

namespace xfile
{
    //-----------------------------------------------------------------------------------------------------
    // Limits
    //-----------------------------------------------------------------------------------------------------
    namespace max_length
    {
        constexpr static int drive_v        = 32;           // How long a drive name can be
        constexpr static int directory_v    = 256;          // How long a path or directory
        constexpr static int file_name_v    = 256;          // File name
        constexpr static int extension_v    = 256;          // Extension
        constexpr static int path_v         = drive_v + directory_v + file_name_v + extension_v;
    };

    //-----------------------------------------------------------------------------------------------------
    // Error handling
    //-----------------------------------------------------------------------------------------------------
    enum class state : std::uint8_t
    { OK                = 0                             // Default OK required by the system
    , FAILURE           = 1                             // Default FAILURE required by the system
    , DEVICE_FAILURE
    , CREATING_FILE
    , OPENING_FILE
    , UNEXPECTED_EOF
    , INCOMPLETE
    };

    //------------------------------------------------------------------------------
    // general functions
    //------------------------------------------------------------------------------
    const std::wstring_view getTempPath             ( void )                        noexcept;
    std::wstring_view       fromPathGetDeviceName   ( std::wstring_view Path )      noexcept;

    //------------------------------------------------------------------------------
    // Description:
    //     This class is the lowest level class for the file system. This class deals
    //     with the most low level platform specific API to do its job. For most part
    //     users will never deal with this class directly. It is intended to be used
    //     by more expert users and low level people. This class defines a device to be 
    //     access by the stream class.
    //------------------------------------------------------------------------------
    struct device
    {
        enum seek_mode
        { SKM_ORIGIN
        , SKM_CURENT
        , SKM_END
        };

        union access_types
        {
            std::uint32_t   m_Value{ 0 };
            struct
            {
                std::uint32_t m_Text          : 2  // This is a text file. Note that this is handle at the top layer. ( 1 for char, 2 for wchar_t)
                            , m_bCreate       : 1  // If not create file then we are accessing an existing file
                            , m_bRead         : 1  // Has read permissions or not
                            , m_bWrite        : 1  // Has write permissions or not
                            , m_bASync        : 1  // Async enable?
                            , m_bCompress     : 1  // Do compress files (been compress)
                            , m_bForceFlush   : 1  // Forces to flush constantly (good for debugging). Note that this is handle at the top layer.
                            ;
            };
        };

        struct instance
        {
            virtual         xerr                open            (std::wstring_view FileName, access_types Flags)            noexcept = 0;
            virtual         void                close           (void)                                                      noexcept = 0;
            virtual         xerr                Read            (std::span<std::byte> View )                                noexcept = 0;
            virtual         xerr                Write           (const std::span<const std::byte> View )                    noexcept = 0;
            virtual         xerr                Seek            (seek_mode Mode, std::size_t Pos)                           noexcept = 0;
            virtual         xerr                Tell            (std::size_t& Pos )                                         noexcept = 0;
            virtual         void                Flush           ()                                                          noexcept = 0;
            virtual         xerr                Length          (std::size_t& L )                                           noexcept = 0;
            virtual         bool                isEOF           (void)                                                      noexcept = 0;
            virtual         xerr                Synchronize     (bool bBlock)                                               noexcept = 0;
            virtual         void                AsyncAbort      (void)                                                      noexcept = 0;
        };

        constexpr                       device          (void)                          noexcept = default;
        virtual                        ~device          (void)                          noexcept = default;
        virtual         void            Init            (const void*)                   noexcept = 0;
        virtual         void            Kill            (void)                          noexcept = 0;
        virtual         instance*       createInstance  (void)                          noexcept = 0;
        virtual         void            destroyInstance (instance& Instance )           noexcept = 0;

        // User must have a global instance of the class to register its device
        struct registration
        {
            registration( const char* pDeviceTitle, device& Device, const char* pDriveNames ) noexcept
                : m_pTitle      { pDeviceTitle }
                , m_pNames      { pDriveNames }
                , m_pNext       { s_pHead }
                , m_pDevice     { &Device }
                { s_pHead = this; }

                            const char*         m_pTitle    { nullptr };
                            const char*         m_pNames    { nullptr };
                            registration*       m_pNext     { nullptr };
                            device*             m_pDevice   { nullptr };
                            std::atomic_int     s_nInUse    {0};
                            std::atomic_int     s_nHaveUsed {0};
            inline static   registration*       s_pHead     { nullptr };
        };
    };

    //------------------------------------------------------------------------------
    // Description:
    //      The stream class is design to be a direct replacement to the fopen. The class
    //      supports most features of the fopen standard plus a few more. The Open function
    //      has been change significantly in order to accommodate the new options. The access
    //      modes are similar BUT not identical to the fopen. Some of the functionality like
    //      the append has drastically change from the standard. Here are the access modes.
    //
    //<TABLE>
    //     Access Mode         Description
    //     =================  ----------------------------------------------------------------------------------------
    //         "r"            Read only                 - the file must exits. Useful when accessing DVDs or other read only media
    //         "r+"           Reading and Writing       - the file must exits
    //         "w" or "w+"    Reading and Writing       - the file will be created.  
    //         "a" or "a+"    Reading and Writing       - the file must exists, and it will do an automatic SeekEnd(0). 
    //  
    //         "@"            Asynchronous Mode         - Allows you to use async features.
    //         "c"            Enable File Compression   - (No Supported) This must be use with 'w'. It will compress at file close.
    //  
    //         "b"            Binary files Mode         - This is the default so you don't need to put it really. 
    //         "t"            Text file Mode            - For writing or Reading text files. If you don't add this assumes you are doing binary files.             
    //                                                      This command basically writes an additional character '\\r' whenever it finds a '\\n' and       
    //                                                      when reading it removes it when it finds '\\r\\n'
    //         "T"            WIDE Text file Mode       - Similar to "t" except it works for unicode...
    //
    //</TABLE>
    //
    //      To illustrate different possible combinations and their meaning we put together a table 
    //      to show a few examples.
    //<TABLE>
    //     Examples of Modes  Description
    //     =================  ----------------------------------------------------------------------------------------
    //          "r"           Good old fashion read a file. Note that you never need to do "rc" as the file system detects that automatically.
    //          "wc"          Means that we want to write a compress file       
    //          "wc@"         Means to create a compress file and we are going to access it asynchronous
    //          "r+@"         Means that we are going to read and write to an already exiting file asynchronously.
    //</TABLE>
    //
    //     Other key change added was the ability to have more type of devices beyond the standard set.
    //     Some of the devices such the net device can be use in conjunction with other devices.
    //     The default device is of course the hard-disk in the PC.
    //<TABLE>
    //     Known devices      Description
    //     =================  ----------------------------------------------------------------------------------------
    //          c:\ d:\ e:\    ...etc local devices such PC drives
    //          ram:\          (No Supported) To use ram as a file device
    //          temp:\         To the temporary folder/drive for the machine
    //          (WIP) dvd:\          (No Supported) To use the dvd system of the console
    //          (WIP) net:\          (No Supported) To access across the network
    //          (WIP) memcard:\      (No Supported) To access memory card file system
    //          (WIP) localhd:\      (No Supported) To access local hard-drives such the ones found in the XBOX
    //          (WIP) buffer:\       (No Supported) User provided buffer data
    //</TABLE>
    //
    //     Example of open strings:
    //<CODE>
    //     open( L"net:\\\\c:\\test.txt",        "r" );      // Reads a file across the network
    //     open( L"ram:\\\\name doesnt matter",  "w" );      // Creates a ram file which you can read/write
    //     open( L"c:\\dumpfile.bin",            "wc" );     // Creates a compress file in you c: drive
    //     open( L"UseDefaultPath.txt",          "r@" );     // No device specify so it reads the file from the default path
    //</CODE>
    //
    //------------------------------------------------------------------------------
    struct stream
    {
        constexpr                               stream          ( void )                                                            noexcept = default;
        inline                                 ~stream          ( void )                                                            noexcept;
                        xerr                    open            ( const std::wstring_view FileName, const char* pMode)              noexcept;
                        void                    close           ( void )                                                            noexcept;
        inline          xerr                    ToFile          ( stream& File )                                                    noexcept;
        inline          xerr                    ToMemory        ( std::span<std::byte> View )                                       noexcept;
        inline          xerr                    Synchronize     ( bool bBlock )                                                     noexcept;
        inline          void                    AsyncAbort      ( void )                                                            noexcept;
        inline          void                    setForceFlush   ( bool bOnOff)                                                      noexcept;
        inline          void                    Flush           ( void)                                                             noexcept;
        inline          xerr                    SeekOrigin      ( std::size_t Offset )                                              noexcept;
        inline          xerr                    SeekEnd         ( std::size_t Offset )                                              noexcept;
        inline          xerr                    SeekCurrent     ( std::size_t Offset )                                              noexcept;
        inline          xerr                    Tell            ( std::size_t& Bytes )                                              noexcept;
        inline          bool                    isEOF           ( void )                                                            noexcept;
        inline          bool                    isOpen          ( void )                                                    const   noexcept { return !!m_pInstance; }
        inline          xerr                    getC            ( int& C )                                                          noexcept;
        inline          xerr                    putC            ( int C, int Count = 1, bool bUpdatePos = true)                     noexcept;
        inline          xerr                    AlignPutC       ( int C, int Count = 0, int Aligment = 4, bool bUpdatePos = true)   noexcept;
        inline          xerr                    getFileLength   ( std::size_t& Length )                                             noexcept;


        inline          xerr                    ReadString      ( std::wstring& Val)                                                noexcept;
        inline          xerr                    ReadString      ( std::string& Val )                                                noexcept;
        inline          xerr                    WriteString     (const std::string_view View)                                       noexcept;
        inline          xerr                    WriteString     (const std::wstring_view View)                                      noexcept;

        template<typename T> requires std::is_trivial_v<T>
        inline          xerr                    Write           ( const T&  Val )                                                   noexcept;

        template<typename T>  requires std::is_trivial_v<T>
        inline          xerr                    WriteSpan       ( std::span<T> A )                                                  noexcept;

        template<typename T, std::size_t T_COUNT_V>  requires std::is_trivial_v<T>
        inline          xerr                    WriteSpan(std::span<T, T_COUNT_V> A)                                                noexcept;

        template<class T>  requires std::is_trivial_v<T>
        inline          xerr                    Read            ( T& Val )                                                          noexcept;

        template<class T>  requires std::is_trivial_v<T>
        inline          xerr                    ReadSpan        ( std::span<T> Span )                                               noexcept;

        template<class T, std::size_t T_COUNT_V>  requires std::is_trivial_v<T>
        inline          xerr                    ReadSpan        ( std::span<T, T_COUNT_V> Span )                                    noexcept;

        inline          bool                    isBinaryMode    ( void )                                                    const   noexcept;
        inline          bool                    isReadMode      ( void )                                                    const   noexcept;
        inline          bool                    isWriteMode     ( void )                                                    const   noexcept;

        template<typename... T_ARGS>
        inline          xerr                    Printf          ( const char* pFormatStr, const T_ARGS& ... Args )                  noexcept;

        template<typename... T_ARGS>
        inline          xerr                    wPrintf         ( const wchar_t* pFormatStr, const T_ARGS& ... Args )               noexcept;

        void                                    Clear           ( void )                                                            noexcept;
        xerr                                    ReadRaw         (std::span<std::byte> View)                                         noexcept;
        xerr                                    WriteRaw        (std::span<const std::byte> View)                                   noexcept;

        device::instance*           m_pInstance     { nullptr };
        device::registration*       m_pDeviceReg    { nullptr };
        device::access_types        m_AccessType    {};
        std::wstring                m_FilePath      {};
    };
}

#include "implementation/xfile_inline.h"

#endif