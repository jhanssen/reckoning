#ifndef PATH_H
#define PATH_H

#include <string>
#include <buffer/Buffer.h>

namespace reckoning {
namespace fs {

class Path
{
public:
    Path();
    Path(const std::string& path);
    Path(const Path& path);
    Path(Path&& path);

    Path& operator=(const Path& path);
    Path& operator=(Path&& path);

    static constexpr const char separator();

    bool isEmpty() const;
    void clear();

    enum Type {
        Error,
        Nonexistant,
        File,
        Directory,
        Special
    };

    Type type() const;
    bool exists() const;

    enum class MkdirMode {
        Normal,
        Recursive
    };
    bool mkdir(MkdirMode mode = MkdirMode::Normal);

    std::string str() const;

    std::shared_ptr<buffer::Buffer> read() const;
    bool write(const std::shared_ptr<buffer::Buffer>& buffer);
    bool remove();

private:
    std::string mPath;
};

inline Path::Path()
{
}

inline Path::Path(const std::string& path)
    : mPath(path)
{
}

inline Path::Path(const Path& path)
    : mPath(path.mPath)
{
}

inline Path::Path(Path&& path)
    : mPath(path.mPath)
{
    path.mPath.clear();
}

inline Path& Path::operator=(const Path& path)
{
    mPath = path.mPath;
    return *this;
}

inline Path& Path::operator=(Path&& path)
{
    mPath = path.mPath;
    path.mPath.clear();
    return *this;
}

inline bool Path::isEmpty() const
{
    return mPath.empty();
}

inline void Path::clear()
{
    mPath.clear();
}

inline constexpr const char Path::separator()
{
#if defined(__WIN32) || defined(__WIN64)
    return '\\';
#else
    return '/';
#endif
}

inline bool Path::exists() const
{
    const auto t = type();
    return t != Nonexistant && t != Error;
}

inline std::string Path::str() const
{
    return mPath;
}

}} // namespace reckoning::fs

#endif // PATH_H
