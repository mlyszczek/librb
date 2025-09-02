/* ==========================================================================
    Licensed under BSD 2clause license. See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */
#ifndef EL_VALID_H
#define EL_VALID_H 1

#include <errno.h>

/* ==========================================================================
    If expression 'x' evaluates to false,  macro will set errno value to 'e'
    and will force function to return with code '-1'
   ========================================================================== */
#define VALID(e, x) if (!(x)) { errno = (e); return -1; }

/* ==========================================================================
    If expression 'x' evaluates to false,  macro will set errno value to 'e'
    and will force function to return value 'v'
   ========================================================================== */
#define VALIDR(e, v, x) if (!(x)) { errno = (e); return (v); }

/* ==========================================================================
    If expression 'x' evaluates to false,  macro will set errno value to 'e'
    and will jump to label 'l'
   ========================================================================== */
#define VALIDGO(e, l, x) if (!(x)) { errno = (e); goto l; }

#endif
