#ifndef ANY_H
#define ANY_H

#include <config.h>

#if defined(HAVE_ANY_CAST)
#include <any>
#elif defined(HAVE_EXPERIMENTAL_ANY_CAST)
#include <experimental/any>

namespace std {
using any = std::experimental::any;
template <class ValueType>
typename add_pointer<ValueType>::type
any_cast(any *a) noexcept
{
    return std::experimental::any_cast<ValueType>(a);
}
template <class ValueType>
typename add_pointer<typename add_const<ValueType>::type>::type
any_cast(any const *a) noexcept
{
    return std::experimental::any_cast<ValueType>(a);
}
template <class ValueType>
ValueType any_cast(any const &a)
{
    return *std::experimental::any_cast<ValueType>(&a);
}
template <class ValueType>
ValueType any_cast(any &a)
{
    return *std::experimental::any_cast<ValueType>(&a);
}
template <class ValueType>
ValueType any_cast(any &&a)
{
    return *std::experimental::any_cast<ValueType>(&a);
}
} // namespace std
#endif // HAVE_ANY_CAST

#endif // ANY_H
