#ifndef CREATABLE_H
#define CREATABLE_H

#include <memory>
#include <util/IsDetected.h>

namespace reckoning {
namespace util {

template<typename T>
using init_t = decltype(std::declval<T&>().init());

template<typename T>
constexpr bool has_init = is_detected<init_t, T>::value;

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

        auto ptr = std::make_shared<EnableMakeShared>(std::forward<Args>(args)...);

        if constexpr (has_init<T>) {
            ptr->init();
        }

        return ptr;
    }
};

}} // namespace reckoning::util

#endif // CREATABLE_H
