#include <fs/Path.h>
#include <buffer/Pool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace reckoning;
using namespace reckoning::fs;

Path::Type Path::type() const
{
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

bool Path::write(const std::shared_ptr<buffer::Buffer>& buffer)
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

    if (fwrite(buffer->data(), buffer->size(), 1, f) != 1) {
        fclose(f);
        return false;
    }
    fclose(f);

    return true;
}
