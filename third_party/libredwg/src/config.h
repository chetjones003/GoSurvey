/* src/config.h.  Generated from cmakeconfig.h.in by cmake.  */

/* Defined to 1 on a cmake build */
#define CMAKE_BUILD 1

/* Defined to <strings.h> or <string.h> if strcasecmp is found */
#ifndef _MSC_VER
#define AX_STRCASECMP_HEADER <strings.h>
#endif

/* Define to one of `_getb67', `GETB67', `getb67' for Cray-2 and Cray-YMP
   systems. This function is required for `alloca.c' support on those systems.
   */
/* #undef CRAY_STACKSEG_END */

/* Define to disable bindings. Not yet supported by cmake */
#define DISABLE_BINDINGS 1

/* Define to disable DXF, JSON and other in/out modules. Not yet supported by cmake. */
/* #undef DISABLE_DXF */

/* Define to disable JSON, GeoJSON in/out modules. */
#define LIBREDWG_DISABLE_JSON
#ifdef LIBREDWG_DISABLE_JSON
#  define DISABLE_JSON
#else
#  undef DISABLE_JSON
#endif   

/* Number of dxf after-comma places. */
/* #undef DXF_PRECISION */

/* Number of GeoJSON after-comma places (recommended 6 by RFC). */
/* #undef GEOJSON_PRECISION */

/* Define if a shared library will be built */
/* #undef ENABLE_SHARED */

/* Define to 1 if mimalloc-override.h should be used. */
/* #undef ENABLE_MIMALLOC */

/* Define if pointers to integers require aligned access */
/* #undef HAVE_ALIGNED_ACCESS_REQUIRED */

/* Define if you have <alloca.h> and it should be used (not on Ultrix).
   */
/* #undef HAVE_ALLOCA_H */
/* Define if you have `alloca', as a function or macro. */
#ifdef HAVE_ALLOCA_H
#  define HAVE_ALLOCA 1
#else
#  undef HAVE_ALLOCA
#endif

/* Define if __attribute__((visibility("default"))) is supported. */
#define HAVE_ATTRIBUTE_VISIBILITY_DEFAULT 1

/* Define if you have the `basename' function. */
#define HAVE_BASENAME 1

/* Defined to 1 when the compiler supports C99, mostly (...) macros */
#define HAVE_C99 1

/* Defined to 1 when the compiler supports C11 */
#if __STDC_VERSION__ >= 201112L
#  define HAVE_C11 1
#endif

/* Define if you have the <ctype.h> header file. */
#define HAVE_CTYPE_H

/* Define if you have the <dirent.h> header file. */
/* #undef HAVE_DIRENT_H */

/* Define if you have the <direct.h> header file. */
#define HAVE_DIRECT_H

/* Define if you have the <endian.h> header file. */
/* #undef HAVE_ENDIAN_H */

/* Define if you have the <sys/endian.h> header file. */
/* #undef HAVE_SYS_ENDIAN_H */

/* Define if you have the <byteorder.h> header file. */
/* #undef HAVE_BYTEORDER_H */

/* Define if you have the <sys/byteorder.h> header file. */
/* #undef HAVE_SYS_BYTEORDER_H */

/* Define if you have the <byteswap.h> header file. */
/* #undef HAVE_BYTESWAP_H */

/* Define if you have the <winsock2.h> header file. */
#define HAVE_WINSOCK2_H

/* Define if you have the <machine/endian.h> header file. */
/* #undef HAVE_MACHINE_ENDIAN_H */

/* Define to 1 if you have the <mimalloc-override.h> header file. */
/* #undef HAVE_MIMALLOC_OVERRIDE_H */

/* Define if you have the <sys/param.h> header file. */
/* #undef HAVE_SYS_PARAM_H */

/* Define to 1 if you have the <sys/time.h> header file. */
/* #undef HAVE_SYS_TIME_H */

/* Define if you have the <float.h> header file. */
#define HAVE_FLOAT_H

/* Define if you have the `floor' function. */
#define HAVE_FLOOR

// TODO versions
#ifdef __GNUC__
/* Define to 1 if the system has the `aligned' function attribute */
#define HAVE_FUNC_ATTRIBUTE_ALIGNED 1

/* Define if the system has the `format' function attribute */
#define HAVE_FUNC_ATTRIBUTE_FORMAT 1

/* Define if the system has the `gnu_format' function attribute */
//#define HAVE_FUNC_ATTRIBUTE_GNU_FORMAT 1

/* Define if the system has the `malloc' function attribute */
#define HAVE_FUNC_ATTRIBUTE_MALLOC 1

/* Define if the system has the `ms_format' function attribute */
//#define HAVE_FUNC_ATTRIBUTE_MS_FORMAT 1

/* Define if the system has the `returns_nonnull' function attribute */
#define HAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL 1
#endif

/* Define if you have the <getopt.h> header file. */
/* #undef HAVE_GETOPT_H */

/* Define if you have the `getopt_long' function. */
#ifdef HAVE_GETOPT_H
#  define HAVE_GETOPT_LONG 1
#else
#  undef HAVE_GETOPT_LONG
#endif

/* Define to 1 if you have the `gettimeofday' function. */
/* #undef HAVE_GETTIMEOFDAY */

/* Define if you have the iconv() function. */
/* #undef HAVE_ICONV */

/* Define if you have the <iconv.h> header file. */
/* #undef HAVE_ICONV_H */

/* Define if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H

/* Define if you have the <libgen.h> header file. */
/* #undef HAVE_LIBGEN_H */

/* Define if you have the `m' library (-lm). */
#define HAVE_LIBM 1

/* Define if you have the <libps/pslib.h> header file. */
/* #undef HAVE_LIBPS_PSLIB_H */

/* Define if you have the <limits.h> header file. */
#define HAVE_LIMITS_H

/* Define if your system has a GNU libc compatible `malloc' function, and
   to 0 otherwise. */
#define HAVE_MALLOC 1

/* Define if you have the <malloc.h> header file. */
#define HAVE_MALLOC_H

/* Define if you have the `memchr' function. */
#define HAVE_MEMCHR

/* Define if you have the `memmem' function. */
/* #undef HAVE_MEMMEM */

/* Define if you have the `memmove' function. */
#define HAVE_MEMMOVE

/* Define if you have the <memory.h> header file. */
#define HAVE_MEMORY_H

/* Define if -lpcre2-16 is used also */
#define HAVE_PCRE2_16

/* Define if you have the <pcre2.h> header file. */
/* #undef HAVE_PCRE2_H */

/* If available, contains the Python version number currently in use. */
/* #undef HAVE_PYTHON */

/* Define if your system has a GNU libc compatible `realloc' function,
   and to 0 otherwise. */
#define HAVE_REALLOC

/* Define if you have the `scandir' function. */
/* #undef HAVE_SCANDIR */

/* Define if you have the `setenv' function. */
/* #undef HAVE_SETENV */

/* Define if you have the `sqrt' function. */
/* #undef HAVE_SQRT */

/* Define if `stat' has the bug that it succeeds when given the
   zero-length file name argument. */
/* #undef HAVE_STAT_EMPTY_STRING_BUG */

/* Define if you have the <stddef.h> header file. */
#define HAVE_STDDEF_H

/* Define if you have the <stdint.h> header file. */
#define HAVE_STDINT_H

/* Define if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H

/* Define if you have the `strcasecmp' function. */
/* #undef HAVE_STRCASECMP */

/* Define if you have the `strcasestr' function. */
/* #undef HAVE_STRCASESTR */

/* Define if you have the `strchr' function. */
#define HAVE_STRCHR

/* Define if you have the <strings.h> header file. */
/* #undef HAVE_STRINGS_H */

/* Define if you have the <string.h> header file. */
#define HAVE_STRING_H

/* Define if you have the `strnlen' function. */
#define HAVE_STRNLEN

/* Define if you have the `strrchr' function. */
#define HAVE_STRRCHR

/* Define if you have the `strtol' function. */
#define HAVE_STRTOL

/* Define if you have the `strtoul' function. */
#define HAVE_STRTOUL

/* Define if you have the `strtoull' function. */
#define HAVE_STRTOULL

/* Define if you have the <sys/stat.h> header file.
   Currently we require this, because all platforms provide it,
   but our clang-cl toolchain doesn't find it.
 */
#define HAVE_SYS_STAT_H

/* Define if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H

/* Define if you have the <unistd.h> header file. */
/* #undef HAVE_UNISTD_H */

/* Define if you have the <valgrind/valgrind.h> header file. */
#undef HAVE_VALGRIND_VALGRIND_H

/* Define if you have the <wchar.h> header file. */
#define HAVE_WCHAR_H

/* Define if you have the `wcscmp' function. */
#define HAVE_WCSCMP

/* Define if you have the `wcscpy' function. */
#define HAVE_WCSCPY

/* Define if you have the `wcslen' function. */
#define HAVE_WCSLEN

/* Define if you have the `wcsnlen' function. */
#define HAVE_WCSNLEN

/* Define if you have the <wctype.h> header file. */
#define HAVE_WCTYPE_H

/* Define if the system has the type `_Bool'. */
#define HAVE__BOOL 1

/* Define if this is a release, skipping unstable DWG features, unknown
   DWG versions and objects. */
/* #undef IS_RELEASE */

/* Define if `lstat' dereferences a symlink specified with a trailing
   slash. */
#define LSTAT_FOLLOWS_SLASHED_SYMLINK 1

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "libredwg@gnu.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "LibreDWG"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "LibreDWG 0.13.3"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "libredwg"

/* Define to the home page for this package. */
#define PACKAGE_URL "https://savannah.gnu.org/projects/libredwg/"

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.13.3"

/* The size of `size_t', as computed by sizeof. */
#define SIZEOF_SIZE_T 8

/* The number of bytes in type wchar_t */
#define SIZEOF_WCHAR_T 2

/* If using the C implementation of alloca, define if you know the
   direction of stack growth for your system; otherwise it will be
   automatically deduced at runtime.
	STACK_DIRECTION > 0 => grows toward higher addresses
	STACK_DIRECTION < 0 => grows toward lower addresses
	STACK_DIRECTION = 0 => direction of growth unknown */
/* #undef STACK_DIRECTION */

/* Define if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to enable runtime tracing support. */
#define USE_TRACING 1

/* Define to disable write support. */
/* #undef LIBREDWG_DISABLE_WRITE */
#ifdef LIBREDWG_DISABLE_WRITE
#  undef USE_WRITE
#else
#  define USE_WRITE
#endif   

/* Needed for strdup */
#define _POSIX_C_SOURCE 200809L

/* Define for Solaris 2.5.1 so the uint32_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef were allowed, the
   #define below would cause a syntax error. */
/* #undef _UINT32_T */

/* Define for Solaris 2.5.1 so the uint64_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef were allowed, the
   #define below would cause a syntax error. */
/* #undef _UINT64_T */

/* Needed for cygwin strdup */
#ifdef __CYGWIN__
#  define __XSI_VISIBLE 700
#endif

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif

/* Define to the type of a signed integer type of width exactly 16 bits if
   such a type exists and the standard includes do not define it. */
/* #undef int16_t */

/* Define to the type of a signed integer type of width exactly 32 bits if
   such a type exists and the standard includes do not define it. */
/* #undef int32_t */

/* Define to the type of a signed integer type of width exactly 64 bits if
   such a type exists and the standard includes do not define it. */
/* #undef int64_t */

/* Define to rpl_malloc if the replacement function should be used. */
/* #undef malloc */

/* Define to rpl_realloc if the replacement function should be used. */
/* #undef realloc */

/* If restrict is broken with this compiler */
#ifdef __cplusplus
  #define restrict
#endif

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Define to the type of an unsigned integer type of width exactly 16 bits if
   such a type exists and the standard includes do not define it. */
/* #undef uint16_t */

/* Define to the type of an unsigned integer type of width exactly 32 bits if
   such a type exists and the standard includes do not define it. */
/* #undef uint32_t */

/* Define to the type of an unsigned integer type of width exactly 64 bits if
   such a type exists and the standard includes do not define it. */
/* #undef uint64_t */
