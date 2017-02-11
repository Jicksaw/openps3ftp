/* ftp.hpp: Include file for linking programs. */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void ftp_server_start(void* server_ptr);
void ftp_server_start_ex(uint64_t server_ptr);
#ifdef __cplusplus
}
#endif
