#include <OpenImageIO/filesystem.h>


#define CPPMM_IGNORE __attribute__((annotate("cppmm:ignore")))
#define CPPMM_RENAME(x) __attribute__((annotate("cppmm:rename:" #x)))
#define CPPMM_MANUAL(x) __attribute__((annotate("cppmm:manual:" #x)))

#define CPPMM_OPAQUEPTR __attribute__((annotate("cppmm:opaqueptr")))
#define CPPMM_VALUETYPE __attribute__((annotate("cppmm:valuetype")))

namespace cppmm_bind {
namespace OIIO_NAMESPACE {
// alias the main library namespace with a convenient short form here
namespace OIIO = ::OIIO_NAMESPACE;

namespace Filesystem {

std::string filename(const std::string& filepath) noexcept;
std::string extension(const std::string& filepath, bool include_dot) noexcept;
std::string parent_path(const std::string& filepath) noexcept;
std::string replace_extension(const std::string& filepath, const std::string& new_extension) noexcept;
// void searchpath_split (const std::string &searchpath, std::vector<std::string> &dirs, bool validonly);
// std::string searchpath_find (const std::string &filename, const std::vector<std::string> &dirs, bool testcwd = true, bool recursive = false);

class IOProxy {
public:
    enum Mode {};

    IOProxy() CPPMM_IGNORE;
    IOProxy(OIIO::string_view filename, OIIO::Filesystem::IOProxy::Mode mode) CPPMM_IGNORE;
    virtual ~IOProxy() CPPMM_RENAME(delete);

    virtual const char* proxytype () const;
    virtual void close ();
    virtual bool opened () const;
    virtual int64_t tell ();
    virtual bool seek (int64_t offset);
    virtual size_t read (void *buf, size_t size);
    virtual size_t write (const void *buf, size_t size);
    virtual size_t pread (void *buf, size_t size, int64_t offset);
    virtual size_t pwrite (const void *buf, size_t size, int64_t offset);
    virtual size_t size ();
    virtual void flush ();

    OIIO::Filesystem::IOProxy::Mode mode ();
    const std::string& filename () const;
    bool seek (int64_t offset, int origin);

} CPPMM_OPAQUEPTR;

class IOMemReader {
    IOMemReader(void* buf, size_t size) CPPMM_RENAME(new);
    virtual ~IOMemReader() CPPMM_RENAME(delete);
    virtual const char* proxytype () const;
    virtual void close ();
    virtual bool opened () const;
    virtual int64_t tell ();
    virtual bool seek (int64_t offset);
    virtual size_t read (void *buf, size_t size);
    virtual size_t write (const void *buf, size_t size);
    virtual size_t pread (void *buf, size_t size, int64_t offset);
    virtual size_t pwrite (const void *buf, size_t size, int64_t offset);
    virtual size_t size ();
    virtual void flush ();

    OIIO::Filesystem::IOProxy::Mode mode ();
    const std::string& filename ();
    bool seek (int64_t offset, int origin);

} CPPMM_OPAQUEPTR;

}
}
}
