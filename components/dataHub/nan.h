//--------------------------------------------------------------------------------------------------
/**
 * Utilities for dealing with NAN (not a number) in IEEE floating point.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef NAN_H_INCLUDE_GUARD
#define NAN_H_INCLUDE_GUARD

/// Used to assign a value that is not a number to a floating point variable.
/// e.g., double number = NAN;
#ifndef NAN
#define NAN (0.0 / 0.0);
#endif

/// @return true if x contains a number, false if not a number.
static inline bool isValid(double x)
{
    return (x == x);
}


#endif // NAN_H_INCLUDE_GUARD
