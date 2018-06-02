#ifndef CREATABLE_H
#define CREATABLE_H

#include <memory>

namespace reckoning {
namespace util {

template <typename T>
class Creatable
{
public:
    template<typename... Args>
    static std::shared_ptr<T> create(Args&& ...args)
    {
        struct EnableMakeShared : public T
        {
        public:
            EnableMakeShared(Args&& ...args)
                : T(std::forward<Args>(args)...)
            {
            }
        };

        return std::make_shared<EnableMakeShared>(std::forward<Args>(args)...);
    }
};

}} // namespace reckoning::util

#endif // CREATABLE_H
