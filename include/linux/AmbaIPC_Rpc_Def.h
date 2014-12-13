/*-------------------------------------------------------------------------------------------------------------------*\
 *  @FileName       :: AmbaIPC_Rpc_Def.h
 *
 *  @Copyright      :: Copyright (C) 2013 Ambarella Corporation. All rights reserved.
 *
 *                     No part of this file may be reproduced, stored in a retrieval system,
 *                     or transmitted, in any form, or by any means, electronic, mechanical, photocopying,
 *                     recording, or otherwise, without the prior consent of Ambarella Corporation.
 *
 *  @Description    :: Common definitions for AmbaIPC RPC program.
 *
 *  @History        ::
 *      Date        Name        			Comments
 *      12/12/2013  Yuan-Ying Chang       	Created
 *
\*-------------------------------------------------------------------------------------------------------------------*/
#ifndef __AMBAIPC_RPC_DEF_H__
#define __AMBAIPC_RPC_DEF_H__


#define AMBA_IPC_MAX_PAYLOAD_SIZE           0x7B0

typedef enum _AMBA_IPC_HOST_e_ {
	AMBA_IPC_HOST_LINUX = 0,
	AMBA_IPC_HOST_THREADX,               
	AMBA_IPC_HOST_MAX
} AMBA_IPC_HOST_e;

typedef enum _AMBA_IPC_REPLY_STATUS_e_ {
	AMBA_IPC_REPLY_SUCCESS = 0,
	AMBA_IPC_REPLY_PROG_UNAVAIL,         
	AMBA_IPC_REPLY_PARA_INVALID,         
	AMBA_IPC_REPLY_SYSTEM_ERROR,
	AMBA_IPC_REPLY_TIMEOUT,
	AMBA_IPC_REPLY_MAX = 0xFFFFFFFF         
} AMBA_IPC_REPLY_STATUS_e;

typedef enum _AMBA_IPC_BINDER_e_ {
	AMBA_IPC_BINDER_BIND = 0,
	AMBA_IPC_BINDER_REGISTER,            
	AMBA_IPC_BINDER_UNREGISTER,
	AMBA_IPC_BINDER_LIST       
} AMBA_IPC_BINDER_e;

typedef enum _AMBA_IPC_MSG_e_ {
	AMBA_IPC_MSG_CALL = 0,
	AMBA_IPC_MSG_REPLY                  	
} AMBA_IPC_MSG_e;

typedef enum _AMBA_IPC_COMMUICATION_MODE_e_ {
	AMBA_IPC_SYNCHRONOUS = 0,
	AMBA_IPC_ASYNCHRONOUS,
	AMBA_IPC_MODE_MAX = 0xFFFFFFFF
} AMBA_IPC_COMMUICATION_MODE_e ;

typedef struct _AMBA_IPC_SVC_RESULT_s_ {
	int Length;
	void *pResult;
	AMBA_IPC_COMMUICATION_MODE_e Mode;
	AMBA_IPC_REPLY_STATUS_e Status;
} AMBA_IPC_SVC_RESULT_s;

/* function pointer prototype for svc procedure */
typedef void (*AMBA_IPC_PROC_f)(void *, AMBA_IPC_SVC_RESULT_s *);

typedef struct _AMBA_IPC_PROC_s_ {
    AMBA_IPC_PROC_f Proc;
    AMBA_IPC_COMMUICATION_MODE_e Mode;
} AMBA_IPC_PROC_s;

typedef struct _AMBA_IPC_PROG_INFO_s_ {
	int ProcNum;
	AMBA_IPC_PROC_s *pProcInfo; 
} AMBA_IPC_PROG_INFO_s;
#endif