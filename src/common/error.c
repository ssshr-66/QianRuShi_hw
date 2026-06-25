#include "common/error.h"

const char *err_str(err_t e)
{
    switch (e) {
    case ERR_OK:       return "OK";
    case ERR_NOMEM:    return "out of memory";
    case ERR_IO:       return "I/O error";
    case ERR_CAPTURE:  return "capture error";
    case ERR_ENCODE:   return "encode error";
    case ERR_PROTOCOL: return "protocol error";
    case ERR_CLOSED:   return "connection closed";
    case ERR_AGAIN:    return "try again";
    case ERR_INVAL:    return "invalid argument";
    default:           return "unknown error";
    }
}
