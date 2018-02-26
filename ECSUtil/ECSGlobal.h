/*
* Copyright (c) 2017, EMC Corporation. All Rights Reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License").
* You may not use this file except in compliance with the License.
* A copy of the License is located at
*
* http://www.apache.org/licenses/LICENSE-2.0.txt
*
* or in the "license" file accompanying this file. This file is distributed
* on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
* express or implied. See the License for the specific language governing
* permissions and limitations under the License.
*/

#pragma once
#include "exportdef.h"
#include "ECSUtil.h"
#include "ECSConnection.h"
#include "NTERRTXT.H"
#include "FileSupport.h"
#include "SimpleWorkerThread.h"

#define ECSUTIL_VERSION "0.9.0"
#define ECSUTIL_VERNUM 0x0900
#define ECSUTIL_VER_MAJOR 0
#define ECSUTIL_VER_MINOR 9
#define ECSUTIL_VER_REVISION 0
#define ECSUTIL_VER_SUBREVISION 0

void ECSUTIL_EXT_API ECSInitLib(DWORD dwGarbageCollectInterval = MINUTES(1));
void ECSUTIL_EXT_API ECSTermLib(void);
