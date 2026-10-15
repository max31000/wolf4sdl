// WL_UTILS.C

#include "wl_utils.h"


/*
===================
=
= safe_malloc
=
= Wrapper for malloc with a NULL check
=
===================
*/

void *safe_malloc (size_t size, const char *fname, uint32_t line)
{
    void *ptr;

    ptr = malloc(size);

    if (!ptr)
    {
        snprintf (str,sizeof(str),"%s",fname);
        Quit ("SafeMalloc: Error allocating %u bytes: %s\nFile: %s Line %u",size,strerror(errno),basename(str),line);
    }

    return ptr;
}


/*
===================
=
= safe_realloc
=
= Wrapper for realloc with a NULL check
=
===================
*/

void *safe_realloc (void *mem, size_t size, const char *fname, uint32_t line)
{
    void *ptr;

    ptr = realloc(mem,size);

    if (!ptr)
    {
        snprintf (str,sizeof(str),"%s",fname);
        Quit ("SafeRealloc: Error re-allocating %u bytes: %s\nFile: %s Line: %u Address: %p",size,strerror(errno),basename(str),line,mem);
    }

    return ptr;
}


/*
===================
=
= safe_free
=
= Wrapper for free with pointer nullification
=
===================
*/

void safe_free (void **ptr)
{
    if (ptr && *ptr)
    {
        free (*ptr);
        *ptr = NULL;
    }
}


fixed FixedMul (fixed a, fixed b)
{
	return (fixed)(((int64_t)a * b + 0x8000) >> FRACBITS);
}

fixed FixedDiv (fixed a, fixed b)
{
	int64_t c = ((int64_t)a << FRACBITS) / (int64_t)b;

	return (fixed)c;
}

uint16_t ReadShort (void *ptr)
{
    unsigned value;
    byte     *work;

    work = ptr;
    value = work[0] | (work[1] << 8);

    return value;
}

uint32_t ReadLong (void *ptr)
{
    uint32_t value;
    byte     *work;

    work = ptr;
    value = work[0] | (work[1] << 8) | (work[2] << 16) | (work[3] << 24);

    return value;
}


void Error (const char *string)
{
#ifdef __EMSCRIPTEN__
    BW_Error (string);      // no alert() box in the browser
#else
    SDL_ShowSimpleMessageBox (SDL_MESSAGEBOX_ERROR,"Wolf4SDL",string,NULL);
#endif
}

void Help (const char *string)
{
    SDL_ShowSimpleMessageBox (SDL_MESSAGEBOX_INFORMATION,"Wolf4SDL",string,NULL);
}
