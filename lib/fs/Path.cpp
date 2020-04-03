#include <fs/Path.h>
#include <buffer/Pool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
# include <mach-o/dyld.h>
# include <limits.h>
#endif

using namespace reckoning;
using namespace reckoning::fs;

Path::Type Path::type() const
{
    if (mPath.empty())
        return Error;
    struct stat st;
    if (::stat(mPath.c_str(), &st) == -1) {
        if (errno == ENOENT)
            return Nonexistant;
        return Error;
    }

    switch (st.st_mode & S_IFMT) {
    case S_IFREG:
        return File;
    case S_IFDIR:
        return Directory;
    case S_IFSOCK:
    case S_IFLNK:
    case S_IFBLK:
    case S_IFCHR:
    case S_IFIFO:
        return Special;
    }

    return Nonexistant;
}

bool Path::mkdir(MkdirMode mode)
{
    static constexpr const char sep = Path::separator();

    auto process = [](const std::string& p, int mode) -> bool {
        if (p.empty())
            return true;
        struct stat st;
        if (::stat(p.c_str(), &st) == -1) {
            if (errno == ENOENT) {
                // try to make the path
                if (::mkdir(p.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1)
                    return false;
                return true;
            }
        } else {
            if (S_ISDIR(st.st_mode)) {
                // this is not ideal
                if (::access(p.c_str(), mode) == 0)
                    return true;
            }
        }
        return false;
    };

    const size_t len = mPath.size();

    if (mode == MkdirMode::Recursive) {
        // walk the paths. this doesn't handle quoted or escaped separators
        for (size_t i = 0; i < len; ++i) {
            switch (mPath[i]) {
            case sep:
                if (!process(mPath.substr(0, i), R_OK | X_OK))
                    return false;
                break;
            }
        }
    }

    return process(mPath.substr(0, len), R_OK | W_OK | X_OK);
}

bool Path::remove()
{
    return unlink(mPath.c_str()) == 0;
}

std::shared_ptr<buffer::Buffer> Path::read() const
{
    FILE* f = fopen(mPath.c_str(), "r");
    if (!f)
        return std::shared_ptr<buffer::Buffer>();
    const int fd = fileno(f);
    struct stat st;
    if (::fstat(fd, &st) == -1 || !S_ISREG(st.st_mode)) {
        fclose(f);
        return std::shared_ptr<buffer::Buffer>();
    }

    auto buffer = buffer::Pool<4, 16384>::pool().get(st.st_size);
    if (fread(buffer->data(), st.st_size, 1, f) != 1) {
        // also bad
        fclose(f);
        return std::shared_ptr<buffer::Buffer>();
    }
    fclose(f);

    return buffer;
}

bool Path::write(const void* data, size_t size)
{
    FILE* f = fopen(mPath.c_str(), "w");
    if (!f)
        return false;
    const int fd = fileno(f);
    struct stat st;
    if (::fstat(fd, &st) == -1 || !S_ISREG(st.st_mode)) {
        fclose(f);
        return false;
    }

    if (fwrite(data, size, 1, f) != 1) {
        fclose(f);
        return false;
    }
    fclose(f);

    return true;
}

bool Path::write(const std::shared_ptr<buffer::Buffer>& buffer)
{
    return write(buffer->data(), buffer->size());
}

Path Path::applicationPath()
{
#ifdef __APPLE__
    std::string buf;
    buf.resize(1024);
    for (;;) {
        uint32_t bufsize = buf.size();
        const int r = _NSGetExecutablePath(&buf[0], &bufsize);
        if (r == 0) {
            buf.resize(bufsize);
            return Path(std::move(buf));
        } else if (r == -1 && bufsize > static_cast<int>(buf.size())) {
            // try again
            buf.resize(bufsize);
        } else {
            // bail out
            return Path();
        }
    }
#else
    // assume linux?
    std::string buf;
    buf.resize(1024); // good starting point?
    for (;;) {
        const ssize_t r = readlink("/proc/self/exe", &buf[0], buf.size());
        if (r == static_cast<ssize_t>(buf.size())) {
            // truncated? try again until we know for sure
            buf.resize(buf.size() * 2);
        } else if (r > 0) {
            buf.resize(r);
            return Path(std::move(buf));
        } else {
            // something bad happened
            return Path();
        }
    }
#endif
    return Path();
}
