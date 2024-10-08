/* Automatically generated nanopb constant definitions */
/* Generated by nanopb-0.3.9.8 at Fri Aug  9 21:47:15 2024. */

#include "backup.pb.h"

/* @@protoc_insertion_point(includes) */
#if PB_PROTO_HEADER_VERSION != 30
#error Regenerate this file with the current version of nanopb generator.
#endif



const pb_field_t Config_fields[4] = {
    PB_FIELD(  1, UINT64  , SINGULAR, STATIC  , FIRST, Config, version, version, 0),
    PB_FIELD(  2, STRING  , SINGULAR, CALLBACK, OTHER, Config, content, version, 0),
    PB_FIELD(  3, STRING  , SINGULAR, CALLBACK, OTHER, Config, md5, content, 0),
    PB_LAST_FIELD
};

const pb_field_t Instance_fields[4] = {
    PB_FIELD(  1, STRING  , SINGULAR, CALLBACK, FIRST, Instance, host, host, 0),
    PB_FIELD(  2, INT32   , SINGULAR, STATIC  , OTHER, Instance, port, host, 0),
    PB_FIELD(  3, INT32   , SINGULAR, STATIC  , OTHER, Instance, weight, port, 0),
    PB_LAST_FIELD
};

const pb_field_t Service_fields[3] = {
    PB_FIELD(  1, UINT64  , SINGULAR, STATIC  , FIRST, Service, version, version, 0),
    PB_FIELD(  2, MESSAGE , REPEATED, CALLBACK, OTHER, Service, instances, version, &Instance_fields),
    PB_LAST_FIELD
};


/* @@protoc_insertion_point(eof) */
