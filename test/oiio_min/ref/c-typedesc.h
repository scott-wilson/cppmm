#pragma once

#include "cppmm_containers.h"


#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#define CPPMM_ALIGN(x) __declspec(align(x))
#else
#define CPPMM_ALIGN(x) __attribute__((aligned(x)))
#endif

typedef struct {
    unsigned char basetype;
    unsigned char aggregate;
    unsigned char vecsemantics;
    unsigned char reserved;
    int arraylen;
} OIIO_TypeDesc;


typedef struct { char _private[24]; } OIIO_TypeDesc_vector CPPMM_ALIGN(8);

void OIIO_TypeDesc_vector_ctor(OIIO_TypeDesc_vector* vec);
void OIIO_TypeDesc_vector_dtor(const OIIO_TypeDesc_vector* vec);
int OIIO_TypeDesc_vector_size(const OIIO_TypeDesc_vector* vec);
OIIO_TypeDesc* OIIO_TypeDesc_vector_data(OIIO_TypeDesc_vector* vec);

void OIIO_TypeDesc_vector_get(const OIIO_TypeDesc_vector* vec, int index, OIIO_TypeDesc* element);
void OIIO_TypeDesc_vector_set(OIIO_TypeDesc_vector* vec, int index, OIIO_TypeDesc* element);
enum OIIO_TypeDesc_VECSEMANTICS {
    OIIO_TypeDesc_VECSEMANTICS_NOXFORM = 0,
    OIIO_TypeDesc_VECSEMANTICS_NOSEMANTICS = 0,
    OIIO_TypeDesc_VECSEMANTICS_COLOR = 1,
    OIIO_TypeDesc_VECSEMANTICS_POINT = 2,
    OIIO_TypeDesc_VECSEMANTICS_VECTOR = 3,
    OIIO_TypeDesc_VECSEMANTICS_NORMAL = 4,
    OIIO_TypeDesc_VECSEMANTICS_TIMECODE = 5,
    OIIO_TypeDesc_VECSEMANTICS_KEYCODE = 6,
    OIIO_TypeDesc_VECSEMANTICS_RATIONAL = 7,
};

enum OIIO_TypeDesc_BASETYPE {
    OIIO_TypeDesc_BASETYPE_UNKNOWN = 0,
    OIIO_TypeDesc_BASETYPE_NONE = 1,
    OIIO_TypeDesc_BASETYPE_UINT8 = 2,
    OIIO_TypeDesc_BASETYPE_UCHAR = 2,
    OIIO_TypeDesc_BASETYPE_INT8 = 3,
    OIIO_TypeDesc_BASETYPE_CHAR = 3,
    OIIO_TypeDesc_BASETYPE_UINT16 = 4,
    OIIO_TypeDesc_BASETYPE_USHORT = 4,
    OIIO_TypeDesc_BASETYPE_INT16 = 5,
    OIIO_TypeDesc_BASETYPE_SHORT = 5,
    OIIO_TypeDesc_BASETYPE_UINT32 = 6,
    OIIO_TypeDesc_BASETYPE_UINT = 6,
    OIIO_TypeDesc_BASETYPE_INT32 = 7,
    OIIO_TypeDesc_BASETYPE_INT = 7,
    OIIO_TypeDesc_BASETYPE_UINT64 = 8,
    OIIO_TypeDesc_BASETYPE_ULONGLONG = 8,
    OIIO_TypeDesc_BASETYPE_INT64 = 9,
    OIIO_TypeDesc_BASETYPE_LONGLONG = 9,
    OIIO_TypeDesc_BASETYPE_HALF = 10,
    OIIO_TypeDesc_BASETYPE_FLOAT = 11,
    OIIO_TypeDesc_BASETYPE_DOUBLE = 12,
    OIIO_TypeDesc_BASETYPE_STRING = 13,
    OIIO_TypeDesc_BASETYPE_PTR = 14,
    OIIO_TypeDesc_BASETYPE_LASTBASE = 15,
};

enum OIIO_TypeDesc_AGGREGATE {
    OIIO_TypeDesc_AGGREGATE_SCALAR = 1,
    OIIO_TypeDesc_AGGREGATE_VEC2 = 2,
    OIIO_TypeDesc_AGGREGATE_VEC3 = 3,
    OIIO_TypeDesc_AGGREGATE_VEC4 = 4,
    OIIO_TypeDesc_AGGREGATE_MATRIX33 = 9,
    OIIO_TypeDesc_AGGREGATE_MATRIX44 = 16,
};



#undef CPPMM_ALIGN

#ifdef __cplusplus
}
#endif
    