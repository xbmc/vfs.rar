set(SOURCES archive.cpp
            arcread.cpp
            blake2s.cpp
            cmddata.cpp
            consio.cpp
            crc.cpp
            crypt.cpp
            dll.cpp
            encname.cpp
            errhnd.cpp
            extinfo.cpp
            extract.cpp
            filcreat.cpp
            file.cpp
            filefn.cpp
            filestr.cpp
            find.cpp
            getbits.cpp
            global.cpp
            hash.cpp
            headers.cpp
            isnt.cpp
            list.cpp
            log.cpp
            match.cpp
            options.cpp
            pathfn.cpp
            qopen.cpp
            rar.cpp
            rarvm.cpp
            rawread.cpp
            rdwrfn.cpp
            recvol.cpp
            resource.cpp
            rijndael.cpp
            rs.cpp
            rs16.cpp
            scantree.cpp
            secpassword.cpp
            sha1.cpp
            sha256.cpp
            smallfn.cpp
            strfn.cpp
            strlist.cpp
            system.cpp
            threadpool.cpp
            timefn.cpp
            ui.cpp
            unicode.cpp
            unpack.cpp
            volume.cpp)

add_library(unrarxlib STATIC ${SOURCES})
set_property(TARGET unrarxlib PROPERTY POSITION_INDEPENDENT_CODE ON)
if(NOT CORE_SYSTEM_NAME MATCHES windows)
  target_compile_definitions(unrarxlib PRIVATE -DSILENT -DRARDLL)
else()
  target_compile_definitions(unrarxlib PRIVATE -Dstrcasecmp=_stricmp
                                               -D_CRT_NONSTDC_NO_DEPRECATE
                                               -DSILENT
                                               -DRARDLL)
  target_compile_options(unrarxlib PRIVATE /wd4146)
endif()
