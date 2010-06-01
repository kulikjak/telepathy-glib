#ifndef TP_TESTS_MYASSERT_H
#define TP_TESTS_MYASSERT_H

#include <glib.h>
#include <telepathy-glib/util.h>

#define MYASSERT(assertion, extra_format, ...)\
  G_STMT_START {\
      if (!(assertion))\
        {\
          g_error ("\n%s:%d: Assertion failed: %s" extra_format,\
            __FILE__, __LINE__, #assertion, ##__VA_ARGS__);\
        }\
  } G_STMT_END

#define MYASSERT_SAME_UINT(left, right) \
  g_assert_cmpuint ((left), ==, (right))

#endif
