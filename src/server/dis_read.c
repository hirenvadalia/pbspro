/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */
/**
 * dis_read.c - contains function to read and decode the DIS
 *	encoded requests and replies.
 *
 *	Included public functions are:
 *
 *	decode_DIS_replySvr_inner
 *	decode_DIS_replySvr
 *	decode_DIS_replySvrTPP
 *	wire_decode_batch_request
 *	DIS_reply_read
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "libpbs.h"
#include "list_link.h"
#include "server_limits.h"
#include "attribute.h"
#include "log.h"
#include "pbs_error.h"
#include "credential.h"
#include "batch_request.h"
#include "net_connect.h"


/* External Global Data */

extern char	*msg_nosupport;

/**
 * @brief
 * 		decode_DIS_replySvr_inner() - decode a Batch Protocol Reply Structure for Server
 *
 *		This routine decodes a batch reply into the form used by server.
 *		The only difference between this and the command version is on status
 *		replies.  For the server,  the attributes are decoded into a list of
 *		server svrattrl structures rather than a commands's attrl.
 *
 * @see
 * 		decode_DIS_replySvrTPP
 *
 * @param[in] sock - socket connection from which to read reply
 * @param[in,out] reply - batch_reply structure defined in libpbs.h, it must be allocated
 *					  by the caller.
 *
 * @return int
 * @retval 0 - success
 * @retval -1 - if brp_choice is wrong
 * @retval non-zero - decode failure error from a DIS routine
 */

static int
decode_DIS_replySvr_inner(int sock, struct batch_reply *reply)
{
	int		      ct;
	struct brp_select    *psel;
	struct brp_select   **pselx;
	struct brp_status    *pstsvr;
	int		      rc = 0;
	size_t		      txtlen;

	/* next decode code, auxcode and choice (union type identifier) */

	reply->brp_code    = disrsi(sock, &rc);
	if (rc) return rc;
	reply->brp_auxcode = disrsi(sock, &rc);
	if (rc) return rc;
	reply->brp_choice  = disrui(sock, &rc);
	if (rc) return rc;


	switch (reply->brp_choice) {

		case BATCH_REPLY_CHOICE_NULL:
			break;	/* no more to do */

		case BATCH_REPLY_CHOICE_Queue:
		case BATCH_REPLY_CHOICE_RdytoCom:
		case BATCH_REPLY_CHOICE_Commit:
			rc = disrfst(sock, PBS_MAXSVRJOBID+1, reply->brp_un.brp_jid);
			if (rc)
				return (rc);
			break;

		case BATCH_REPLY_CHOICE_Select:

			/* have to get count of number of strings first */

			reply->brp_un.brp_select = NULL;
			pselx = &reply->brp_un.brp_select;
			ct = disrui(sock, &rc);
			if (rc) return rc;

			while (ct--) {
				psel = (struct brp_select *)malloc(sizeof(struct brp_select));
				if (psel == 0) return DIS_NOMALLOC;
				psel->brp_next = NULL;
				psel->brp_jobid[0] = '\0';
				rc = disrfst(sock, PBS_MAXSVRJOBID+1, psel->brp_jobid);
				if (rc) {
					(void)free(psel);
					return rc;
				}
				*pselx = psel;
				pselx = &psel->brp_next;
			}
			break;

		case BATCH_REPLY_CHOICE_Status:

			/* have to get count of number of status objects first */

			CLEAR_HEAD(reply->brp_un.brp_status);
			ct = disrui(sock, &rc);
			if (rc) return rc;

			while (ct--) {
				pstsvr = (struct brp_status *)malloc(sizeof(struct brp_status));
				if (pstsvr == 0) return DIS_NOMALLOC;

				CLEAR_LINK(pstsvr->brp_stlink);
				pstsvr->brp_objname[0] = '\0';
				CLEAR_HEAD(pstsvr->brp_attr);

				pstsvr->brp_objtype = disrui(sock, &rc);
				if (rc == 0) {
					rc = disrfst(sock, PBS_MAXSVRJOBID+1,
						pstsvr->brp_objname);
				}
				if (rc) {
					(void)free(pstsvr);
					return rc;
				}
				append_link(&reply->brp_un.brp_status,
					&pstsvr->brp_stlink, pstsvr);
				rc = wire_decode_svrattrl(sock, &pstsvr->brp_attr);
			}
			break;

		case BATCH_REPLY_CHOICE_Text:

			/* text reply */

		  	reply->brp_un.brp_txt.brp_str = disrcs(sock, &txtlen, &rc);
			reply->brp_un.brp_txt.brp_txtlen = txtlen;
			break;

		case BATCH_REPLY_CHOICE_Locate:

			/* Locate Job Reply */

			rc = disrfst(sock, PBS_MAXDEST+1, reply->brp_un.brp_locate);
			break;

		default:
			return -1;
	}

	return rc;
}

/**
 * @brief
 * 		decode a Batch Protocol Reply Structure for Server
 *
 *  	This routine reads reply over TCP by calling decode_DIS_replySvr_inner()
 * 		to read the reply to a batch request. This routine reads the protocol type
 * 		and version before calling decode_DIS_replySvr_inner() to read the rest of
 * 		the reply structure.
 *
 * @see
 *		DIS_reply_read
 *
 * @param[in] sock - socket connection from which to read reply
 * @param[out] reply - The reply structure to be returned
 *
 * @return Error code
 * @retval DIS_SUCCESS(0) - Success
 * @retval !DIS_SUCCESS   - Failure (see dis.h)
 */
int
decode_DIS_replySvr(int sock, struct batch_reply *reply)
{
	int		      rc = 0;
	int		      i;
	/* first decode "header" consisting of protocol type and version */

	i = disrui(sock, &rc);
	if (rc != 0) return rc;
	if (i != PBS_BATCH_PROT_TYPE) return DIS_PROTO;
	i = disrui(sock, &rc);
	if (rc != 0) return rc;
	if (i != PBS_BATCH_PROT_VER) return DIS_PROTO;

	return (decode_DIS_replySvr_inner(sock, reply));
}

/**
 * @brief
 * 	decode a Batch Protocol Reply Structure for Server over TPP stream
 *
 * 	This routine reads data over TPP stream by calling decode_DIS_replySvr_inner()
 * 	to read the reply to a batch request. This routine reads the protocol type
 * 	and version before calling decode_DIS_replySvr_inner() to read the rest of
 * 	the reply structure.
 *
 * @see
 * 	DIS_reply_read
 *
 * @param[in] sock - socket connection from which to read reply
 * @param[out] reply - The reply structure to be returned
 *
 * @return Error code
 * @retval DIS_SUCCESS(0) - Success
 * @retval !DIS_SUCCESS   - Failure (see dis.h)
 */
int
decode_DIS_replySvrTPP(int sock, struct batch_reply *reply)
{
	/* for tpp based connection, header has already been read */
	return (decode_DIS_replySvr_inner(sock, reply));
}

/**
 * @brief
 * 	Read encoded request from the given buffer and decodes it
 * 	into the given request structure
 *
 * @param[in] buf - buffer which holds encoded request
 * @param[in] request - pointer to request structure
 *
 * @return int
 * @retval 0  success
 * @retval -1 on EOF (no request but no error)
 * @retval >0 failure (a PBSE_ number)
 */
int
wire_decode_batch_request(void *buf, breq *request)
{
	int proto_type = 0;
	int proto_ver = 0;
	int rc = PBSE_NONE;
	void *body = NULL;

	/* Decode the Request Header, that will tell the request type */
	rc = wire_decode_batch_req_hdr(buf, request, &proto_type, &proto_ver);
	if (rc != PBSE_NONE || proto_type != ns(ProtType_Batch) || proto_ver > PBS_BATCH_PROT_VER) {
		log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, LOG_DEBUG, __func__,
				"Req Header bad, errno %d, wire error %d", errno, rc);
		return PBSE_PROTOCOL;
	}

	body = (void *)ns(Req_body((ns(Req_table_t))buf));

	/* Decode the Request Body based on the type */
	switch (request->rq_type) {
		case PBS_BATCH_Connect:
			break;

		case PBS_BATCH_Disconnect:
			return (-1);

		case PBS_BATCH_QueueJob:
		case PBS_BATCH_SubmitResv:
			rc = wire_decode_batch_req_queuejob(body, request);
			break;

		case PBS_BATCH_JobCred:
			rc = wire_decode_batch_req_jobcred(body, request);
			break;

		case PBS_BATCH_UserCred:
			rc = wire_decode_batch_req_usercred(body, request);
			break;

		case PBS_BATCH_jobscript:
		case PBS_BATCH_MvJobFile:
			rc = wire_decode_batch_req_jobfile(body, request);
			break;

		case PBS_BATCH_RdytoCommit:
		case PBS_BATCH_Commit:
		case PBS_BATCH_Rerun:
			rc = wire_decode_batch_req_jobid(body, request);
			break;

		case PBS_BATCH_DeleteJob:
		case PBS_BATCH_DeleteResv:
		case PBS_BATCH_ResvOccurEnd:
		case PBS_BATCH_HoldJob:
		case PBS_BATCH_ModifyJob:
		case PBS_BATCH_ModifyJob_Async:
			rc = wire_decode_batch_req_manage(body, request);
			break;

		case PBS_BATCH_MessJob:
			rc = wire_decode_batch_req_messagejob(body, request);
			break;

		case PBS_BATCH_Shutdown:
		case PBS_BATCH_FailOver:
			rc = wire_decode_batch_req_shutdown(body, request);
			break;

		case PBS_BATCH_SignalJob:
			rc = wire_decode_batch_req_signaljob(body, request);
			break;

		case PBS_BATCH_StatusJob:
			rc = wire_decode_batch_req_status(body, request);
			break;

		case PBS_BATCH_PySpawn:
			rc = wire_decode_batch_req_pyspawn(body, request);
			break;

		case PBS_BATCH_Authenticate:
			rc = wire_decode_batch_req_authenticate(body, request);
			break;

#ifndef PBS_MOM
		case PBS_BATCH_RelnodesJob:
			rc = wire_decode_batch_req_relnodesjob(body, request);
			break;

		case PBS_BATCH_LocateJob:
			rc = wire_decode_batch_req_jobid(body, request);
			break;

		case PBS_BATCH_Manager:
		case PBS_BATCH_ReleaseJob:
		case PBS_BATCH_ModifyResv:
			rc = wire_decode_batch_req_manage(body, request);
			break;

		case PBS_BATCH_MoveJob:
		case PBS_BATCH_OrderJob:
			rc = wire_decode_batch_req_movejob(body, request);
			break;

		case PBS_BATCH_RunJob:
		case PBS_BATCH_AsyrunJob:
		case PBS_BATCH_StageIn:
		case PBS_BATCH_ConfirmResv:
			rc = wire_decode_batch_req_run(body, request);
			break;

		case PBS_BATCH_DefSchReply:
			rc = wire_decode_batch_req_defschreply(body, request);
			break;

		case PBS_BATCH_SelectJobs:
		case PBS_BATCH_SelStat:
			rc = wire_decode_batch_req_selectjob(body, request);
			break;

		case PBS_BATCH_StatusNode:
		case PBS_BATCH_StatusResv:
		case PBS_BATCH_StatusQue:
		case PBS_BATCH_StatusSvr:
		case PBS_BATCH_StatusSched:
		case PBS_BATCH_StatusRsc:
		case PBS_BATCH_StatusHook:
			rc = wire_decode_batch_req_status(body, request);
			break;

		case PBS_BATCH_TrackJob:
			rc = wire_decode_batch_req_trackjob(body, request);
			break;

		case PBS_BATCH_Rescq:
		case PBS_BATCH_ReserveResc:
		case PBS_BATCH_ReleaseResc:
			rc = wire_decode_batch_req_rescq(body, request);
			break;

		case PBS_BATCH_RegistDep:
			rc = wire_decode_batch_req_register(body, request);
			break;

		case PBS_BATCH_PreemptJobs:
			rc = wire_decode_batch_req_preemptjobs(body, request);
			break;

#else	/* yes PBS_MOM */

		case PBS_BATCH_CopyHookFile:
			rc = wire_decode_batch_req_copyhookfile(body, request);
			break;

		case PBS_BATCH_DelHookFile:
			rc = wire_decode_batch_req_delhookfile(body, request);
			break;

		case PBS_BATCH_CopyFiles:
		case PBS_BATCH_DelFiles:
			rc = wire_decode_batch_req_copyfiles(body, request);
			break;

		case PBS_BATCH_CopyFiles_Cred:
		case PBS_BATCH_DelFiles_Cred:
			rc = wire_decode_batch_req_copyfiles_cred(body, request);
			break;

		case PBS_BATCH_Cred:
			rc = wire_decode_batch_req_cred(body, request);
			break;

#endif	/* PBS_MOM */

		default:
			log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, LOG_DEBUG, __func__,
					"%s: %d from %s", msg_nosupport, request->rq_type, request->rq_user);
			rc = PBSE_UNKREQ;
			break;
	}

	if (rc == PBSE_NONE) {
		/* Decode the Request Extension, if present */
		rc = wire_decode_batch_req_extend(buf, request);
		if (rc != PBSE_NONE) {
			log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, LOG_DEBUG, __func__,
					"Request type: %d Req Extension bad, error %d", request->rq_type, rc);
			rc = PBSE_PROTOCOL;
		}
	} else if (rc != PBSE_UNKREQ) {
		log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, LOG_DEBUG, __func__,
				"Req Body bad, type %d", request->rq_type);
		rc = PBSE_PROTOCOL;
	}

	return (rc);
}


/**
 * @brief
 * 	top level function to read and decode DIS based batch reply
 *
 * 	Calls decode_DIS_replySvrTPP in case of PROT_TPP and decode_DIS_replySvr
 * 	in case of PROT_TCP to read the reply
 *
 * @see
 *	read_reg_reply, process_Dreply and process_DreplyTPP.
 *
 * @param[in] sock - socket connection from which to read reply
 * @param[out] reply - The reply structure to be returned
 * @param[in] prot - Whether to read over tcp or tpp based connection
 *
 * @return Error code
 * @retval DIS_SUCCESS(0) - Success
 * @retval !DIS_SUCCESS   - Failure (see dis.h)
 */
int
DIS_reply_read(int sock, struct batch_reply *preply, int prot)
{
	if (prot == PROT_TPP)
		return (decode_DIS_replySvrTPP(sock, preply));


	DIS_tcp_funcs();
	return  (decode_DIS_replySvr(sock, preply));
}
