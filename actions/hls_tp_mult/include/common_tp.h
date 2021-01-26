#ifndef __COMMON_TP_H__
#define __COMMON_TP_H__

/*
 * Copyright 2020 International Business Machines
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <osnap_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACTION_TYPE         0x1014300F
#define RELEASE_LEVEL       0x00000010

/* Enumerator holding the transprecision datatype
 * */
typedef enum {
	AP_UINT 	= 0x00,
	AP_INT 		= 0x01,
	AP_UFIXED	= 0x02,
	AP_FIXED	= 0x03,
	IEEE_HALF	= 0x04,
//	BFLOAT16        = 0x05,
	IEEE_FLOAT	= 0x06,
	IEEE_DOUBLE	= 0x07,
	UNSUPPORTED	= 0x08,
} tp_t;  
  
  
typedef struct tp_mult_job {
	struct snap_addr in;	/* input data */
	struct snap_addr out;   /* offset table */
	uint32_t transprecision_mode;
	uint32_t pad1;
	uint64_t pad2;
} tp_mult_job_t;

#define MAX_NB_OF_TP_READ  16
typedef float  mat_elmt_t; 	// change to float or double depending on your needs



#ifdef __cplusplus
}
#endif

#endif	/* __COMMON_TP_H__*/
