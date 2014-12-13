/*
 * include/linux/aipc/ipc_mutex.h
 *
 * Authors:
 *	Joey Li <jli@ambarella.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * Copyright (C) 2013-2015, Ambarella Inc.
 */

#ifndef __AIPC_MUTEX_H__
#define __AIPC_MUTEX_H__

#include <linux/aipc/ipc_mutex_def.h>

void aipc_mutex_lock(int id);
void aipc_mutex_unlock(int id);

#endif	/* __AIPC_MUTEX_H__ */

