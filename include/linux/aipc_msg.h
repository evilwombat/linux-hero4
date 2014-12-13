/*
 * aipc_msg.h: defines AIPC message format shared by all hosts
 *
 * Authors: Joey Li <jli@ambarella.com
 *
 * Copyright (C) 2013, Ambarella Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __AIPC_MSG_H__
#define __AIPC_MSG_H__

#include "AmbaIPC_Rpc_Def.h"

#define AIPC_HOST_LINUX                 AMBA_IPC_HOST_LINUX
#define AIPC_HOST_THREADX               AMBA_IPC_HOST_THREADX
#define AIPC_HOST_MAX                   AMBA_IPC_HOST_MAX

#define AIPC_BINDING_PORT               111
#define AIPC_CLIENT_NR_MAX              8

#define AIPC_MSG_CALL                   AMBA_IPC_MSG_CALL
#define AIPC_MSG_REPLY                  AMBA_IPC_MSG_REPLY

#define AIPC_REPLY_SUCCESS              AMBA_IPC_REPLY_SUCCESS
#define AIPC_REPLY_PROG_UNAVAIL         AMBA_IPC_REPLY_PROG_UNAVAIL
#define AIPC_REPLY_PARA_INVALID         AMBA_IPC_REPLY_PARA_INVALID
#define AIPC_REPLY_SYSTEM_ERROR         AMBA_IPC_REPLY_SYSTEM_ERROR

#define AIPC_MAX_PAYLOAD_SIZE           AMBA_IPC_MAX_PAYLOAD_SIZE

/* if RPC_DEBUG is on, please also enable RPMSG_DEBUG.
RPMSG_DEBUG is defined in remoteproc.h */
#define RPC_DEBUG						1

struct aipc_msg_call {
	int  prog;              /* program number   */
	int  vers;              /* version number   */
	int  proc;              /* procedure number */
};

struct aipc_msg_reply {
	AMBA_IPC_REPLY_STATUS_e  status;            /* reply status     */
};

struct aipc_msg {
	int  type;              /* body type: call/reply */
	union {
		struct aipc_msg_call    call;
		struct aipc_msg_reply   reply;
	} u;
	int  parameters[0];
};

struct aipc_xprt {
	unsigned char   client_addr;      /* client address */
	unsigned char   server_addr;      /* server address */
	unsigned short  xid;              /* transaction ID */
	unsigned int    client_port;      /* client port    */
	unsigned int    server_port;      /* server port    */
	int             private; 
#if RPC_DEBUG
	/* RPC profiling in ThreadX side */
	unsigned int	tx_rpc_send_start;	
	unsigned int	tx_rpc_send_end;
	unsigned int	tx_rpc_recv_start;
	unsigned int	tx_rpc_recv_end;
	/* RPC profiling in Linux */
	unsigned int	lk_to_lu_start;
	unsigned int	lk_to_lu_end;
	unsigned int	lu_to_lk_start;
	unsigned int	lu_to_lk_end;
#endif
};

struct aipc_pkt {
	struct aipc_xprt xprt;
	struct aipc_msg  msg;
};

#define AIPC_HDRLEN     ((sizeof(struct aipc_pkt)+3)&~3)

#endif //__AIPC_MSG_H__

