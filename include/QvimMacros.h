#ifndef QVIMMACROS_H
#define QVIMMACROS_H

#include <QtGlobal>

// Copy/move suppression owned by qvim. Call sites use this instead of Qt's
// Q_DISABLE_COPY_MOVE so the dependency on the toolkit macro lives in exactly
// one place — swapping the underlying toolkit means editing only this line.
// A macro that declares deleted special members cannot be a constexpr function.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define QVIM_DISABLE_COPY_MOVE(Class) Q_DISABLE_COPY_MOVE(Class)

#endif
