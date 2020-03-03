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

        constexpr bool hasInit = requires(const T& t) {
            t.init();
        };

        auto ptr = std::make_shared<EnableMakeShared>(std::forward<Args>(args)...);

        if (hasInit) {
            ptr->init();
        }

        return ptr;
    }
};

}} // namespace reckoning::util

#endif // CREATABLE_H
