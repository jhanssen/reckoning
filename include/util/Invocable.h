#ifndef INVOCABLE_H
#define INVOCABLE_H

#include <config.h>

#if !defined(HAVE_INVOCABLE_R) && defined(HAVE_INVOKABLE_R)
namespace std {
template <class _Ret, class _Fp, class ..._Args>
using is_invocable_r = __invokable_r<_Ret, _Fp, _Args...>;
}
#endif

#endif // INVOCABLE_H
