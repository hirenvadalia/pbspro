/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of both the OpenPBS software ("OpenPBS")
 * and the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * OpenPBS is free software. You can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * OpenPBS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * PBS Pro is commercially licensed software that shares a common core with
 * the OpenPBS software.  For a copy of the commercial license terms and
 * conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
 * Altair Legal Department.
 *
 * Altair's dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of OpenPBS and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair's trademarks, including but not limited to "PBS™",
 * "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
 * subject to Altair's trademark licensing policies.
 */

/**
 * @file	req_signaljob.c
 *
 * @brief
 * 		req_signaljob.c - functions dealing with sending a signal
 *		     to a running job.
 *
 * Functions included are:
 * 	req_signaljob()
 * 	req_signaljob2()
 * 	issue_signal()
 * 	post_signal_req()
 *
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include "libpbs.h"
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "server.h"
#include "credential.h"
#include "batch_request.h"
#include "job.h"
#include "work_task.h"
#include "pbs_error.h"
#include "log.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "sched_cmds.h"
#include "pbs_sched.h"
#include "acct.h"


/* Private Function local to this file */

void post_signal_req(struct work_task *);
static void req_signaljob2(struct batch_request *preq, job *pjob);
void set_admin_suspend(job *pjob, int set_remove_nstate);
int create_resreleased (job *pjob);

/* Global Data Items: */

extern char *msg_momreject;
extern char *msg_signal_job;
extern job  *chk_job_request(char *, struct batch_request *, int *, int *);


/**
 * @brief
 * 		req_signaljob - service the Signal Job Request
 * @par
 *		This request sends (via MOM) a signal to a running job.
 *
 * @param[in]	preq	-	Signal Job Request
 */

void
req_signaljob(struct batch_request *preq)
{
	int anygood = 0;
	int i;
	char jid[PBS_MAXSVRJOBID + 1];
	int jt; /* job type */
	int offset;
	char *pc;
	job *pjob;
	job *parent;
	char *range;
	int suspend = 0;
	int resume = 0;
	char *vrange;
	int start;
	int end;
	int step;
	int count;
	int err = PBSE_NONE;

	snprintf(jid, sizeof(jid), "%s", preq->rq_ind.rq_signal.rq_jid);

	parent = chk_job_request(jid, preq, &jt, &err);
	if (parent == NULL) {
		pjob = find_job(jid);
		if (pjob != NULL && pjob->ji_pmt_preq != NULL)
			reply_preempt_jobs_request(err, PREEMPT_METHOD_SUSPEND, pjob);
		return; /* note, req_reject already called */
	}

	if (strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_RESUME) == 0 || strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_ADMIN_RESUME) == 0)
		resume = 1;
	else if (strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_SUSPEND) == 0 || strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_ADMIN_SUSPEND) == 0)
		suspend = 1;


	if (suspend || resume) {

		if ((preq->rq_perm & (ATR_DFLAG_OPRD | ATR_DFLAG_OPWR |
			ATR_DFLAG_MGRD | ATR_DFLAG_MGWR)) == 0) {
			/* for suspend/resume, must be mgr/op */
			req_reject(PBSE_PERM, 0, preq);
			return;
		}
	}

	if (jt == IS_ARRAY_NO) {

		/* just a regular job, pass it on down the line and be done */

		req_signaljob2(preq, parent);
		return;

	} else if (jt == IS_ARRAY_Single) {
		char sjst;

		/* single subjob, if running can signal */

		offset = subjob_index_to_offset(parent, get_index_from_jid(jid));
		if (offset == -1) {
			req_reject(PBSE_UNKJOBID, 0, preq);
			return;
		}
		sjst = get_subjob_state(parent, offset);
		if (sjst == -1) {
			req_reject(PBSE_IVALREQ, 0, preq);
			return;
		} else if (sjst == JOB_STATE_LTR_RUNNING) {
			if ((pjob = parent->ji_ajtrk->tkm_tbl[offset].trk_psubjob)) {
				req_signaljob2(preq, pjob);
			} else {
				req_reject(PBSE_BADSTATE, 0, preq);
				return;
			}
		} else {
			req_reject(PBSE_BADSTATE, 0, preq);
			return;
		}
		return;

	} else if (jt == IS_ARRAY_ArrayJob) {

		/* The Array Job itself ... */

		if (!check_job_state(parent, JOB_STATE_LTR_BEGUN)) {
			req_reject(PBSE_BADSTATE, 0, preq);
			return;
		}

		/* for each subjob that is running, signal it via req_signaljob2 */

		++preq->rq_refct;	/* protect the request/reply struct */

		for (i=0; i<parent->ji_ajtrk->tkm_ct; i++) {
			if (get_subjob_state(parent, i) == JOB_STATE_LTR_RUNNING) {
				if ((pjob = parent->ji_ajtrk->tkm_tbl[i].trk_psubjob)) {
					/* if suspending,  skip those already suspended,  */
					if (suspend && (pjob->ji_qs.ji_svrflags & JOB_SVFLG_Suspend))
						continue;
					/* if resuming, skip those not suspended         */
					if (resume && !(pjob->ji_qs.ji_svrflags & JOB_SVFLG_Suspend))
						continue;

					dup_br_for_subjob(preq, pjob, req_signaljob2);
				}
			}
		}
		/* if not waiting on any running subjobs, can reply; else */
		/* it is taken care of when last running subjob responds  */
		if (--preq->rq_refct == 0)
			reply_send(preq);
		return;

	}
	/* what's left to handle is a range of subjobs, foreach subjob 	*/
	/* if running, signal it					*/

	range = get_index_from_jid(jid);
	if (range == NULL) {
		req_reject(PBSE_IVALREQ, 0, preq);
		return;
	}

	/* first check that any in the subrange are in fact running */

	vrange = range;
	while (1) {
		if ((i = parse_subjob_index(vrange, &pc, &start, &end, &step, &count)) == -1) {
			req_reject(PBSE_IVALREQ, 0, preq);
			return;
		} else if (i == 1)
			break;
		for (i = start; i <= end; i += step) {
			char sjst = get_subjob_state(parent, numindex_to_offset(parent, i));

			if (sjst == JOB_STATE_LTR_RUNNING)
				anygood++;
		}
		vrange = pc;
	}
	if (anygood == 0) { /* no running subjobs in the range */
		req_reject(PBSE_BADSTATE, 0, preq);
		return;
	}

	/* now do the deed */

	++preq->rq_refct;	/* protect the request/reply struct */

	while (1) {
		if ((i = parse_subjob_index(range, &pc, &start, &end, &step, &count)) == -1) {
			req_reject(PBSE_IVALREQ, 0, preq);
			break;
		} else if (i == 1)
			break;
		for (i = start; i <= end; i += step) {
			char sjst;
			int idx = numindex_to_offset(parent, i);

			sjst = get_subjob_state(parent, idx);
			if (sjst == JOB_STATE_LTR_RUNNING) {
				if ((pjob = parent->ji_ajtrk->tkm_tbl[idx].trk_psubjob)) {
					dup_br_for_subjob(preq, pjob, req_signaljob2);
				}
			}
		}
		range = pc;
	}

	/* if not waiting on any running subjobs, can reply; else */
	/* it is taken care of when last running subjob responds  */
	if (--preq->rq_refct == 0)
		reply_send(preq);
	return;
}
/**
 * @brief
 * 		req_signaljob2 - service the Signal Job Request
 * @par
 *		This request sends (via MOM) a signal to a running job.
 *
 * @param[in]	preq	-	Signal Job Request
 */
static void
req_signaljob2(struct batch_request *preq, job *pjob)
{
	int rc;
	char *pnodespec;
	int suspend = 0;
	int resume = 0;
	pbs_sched *psched;

	if (!check_job_state(pjob, JOB_STATE_LTR_RUNNING) ||
		(check_job_state(pjob, JOB_STATE_LTR_RUNNING) && check_job_substate(pjob, JOB_SUBSTATE_PROVISION))) {
		req_reject(PBSE_BADSTATE, 0, preq);
		return;
	}
	if ((strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_ADMIN_RESUME) == 0 && !(pjob->ji_qs.ji_svrflags & JOB_SVFLG_AdmSuspd)) ||
		(strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_RESUME) == 0 && (pjob->ji_qs.ji_svrflags & JOB_SVFLG_AdmSuspd))) {
		req_reject(PBSE_WRONG_RESUME, 0, preq);
		return;
	}

	if (strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_RESUME) == 0 || strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_ADMIN_RESUME) == 0)
		resume = 1;
	else if (strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_SUSPEND) == 0 || strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_ADMIN_SUSPEND) == 0)
		suspend = 1;

	/* Special pseudo signals for suspend and resume require op/mgr */

	if (suspend || resume) {

		preq->rq_extra = pjob;	/* save job ptr for post_signal_req() */

		sprintf(log_buffer, "%s job by %s@%s",
			preq->rq_ind.rq_signal.rq_signame,
			preq->rq_user, preq->rq_host);

		log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_JOB, LOG_INFO,
			pjob->ji_qs.ji_jobid, log_buffer);

		if (resume) {
			if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_Suspend) != 0) {

				if (preq->rq_fromsvr == 1 ||
				    strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_ADMIN_RESUME) == 0) {
					/* from Scheduler, resume job */
					pnodespec = get_jattr_str(pjob, JOB_ATR_exec_vnode);
					if (pnodespec) {
						rc = assign_hosts(pjob, pnodespec, 0);
						if (rc == 0) {
							set_resc_assigned((void *)pjob, 0, INCR);
							/* if resume fails,need to free resources */
						} else {
							req_reject(rc, 0, preq);
							return;
						}
					}
					if (is_jattr_set(pjob, JOB_ATR_exec_vnode_deallocated)) {

						char	*hoststr = NULL;
						char	*hoststr2 = NULL;
						char	*vnodestoalloc = NULL;
						char	*new_exec_vnode_deallocated;
	 					new_exec_vnode_deallocated =
		  					get_jattr_str(pjob, JOB_ATR_exec_vnode_deallocated);

						rc = set_nodes((void *)pjob, JOB_OBJECT, new_exec_vnode_deallocated, &vnodestoalloc, &hoststr, &hoststr2, 1, FALSE);
						if (rc != 0) {
							req_reject(rc, 0, preq);
							log_event(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, LOG_WARNING,
							  pjob->ji_qs.ji_jobid, "Warning: Failed to make some nodes aware of deleted job");
						}
					}
				} else {
					/* not from scheduler, change substate so the  */
					/* scheduler will resume the job when possible */
					svr_setjobstate(pjob, JOB_STATE_LTR_RUNNING, JOB_SUBSTATE_SCHSUSP);
					if (find_assoc_sched_jid(pjob->ji_qs.ji_jobid, &psched))
						set_scheduler_flag(SCH_SCHEDULE_NEW, psched);
					else {
						sprintf(log_buffer, "Unable to reach scheduler associated with job %s", pjob->ji_qs.ji_jobid);
						log_err(-1, __func__, log_buffer);
					}
					reply_send(preq);
					return;
				}
			} else {
				/* Job can be resumed only on suspended state */
				req_reject(PBSE_BADSTATE, 0, preq);
				return;
			}
		}
	}

	/* log and pass the request on to MOM */

	sprintf(log_buffer, msg_signal_job, preq->rq_ind.rq_signal.rq_signame,
		preq->rq_user, preq->rq_host);
	log_event(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, LOG_INFO,
		pjob->ji_qs.ji_jobid, log_buffer);
	rc = relay_to_mom(pjob, preq, post_signal_req);
	if (rc) {
		if (resume)
			rel_resc(pjob);
		req_reject(rc, 0, preq);	/* unable to get to MOM */
	}

	/* After MOM acts and replies to us, we pick up in post_signal_req() */
}

/**
 * @brief
 * 		issue_signal - send an internally generated signal to a running job
 *
 * @param[in,out]	pjob	-	running job
 * @param[in]	signame	-	name of the signal to send
 * @param[in]	func	-	function pointer taking work_task structure as argument.
 * @param[in]	extra	-	extra parameter to be stored in sig request
 * @param[in]	nest	-	pointer to the nested batch_request (if any)
 *
 * @return	int
 * @retval	0	- success
 * @retval	non-zero	- error code
 */

int
issue_signal(job *pjob, char *signame, void (*func)(struct work_task *), void *extra)
{
	struct batch_request *newreq;

	/* build up a Signal Job batch request */

	if ((newreq = alloc_br(PBS_BATCH_SignalJob)) == NULL)
		return (PBSE_SYSTEM);

	newreq->rq_extra = extra;

	strcpy(newreq->rq_ind.rq_signal.rq_jid, pjob->ji_qs.ji_jobid);
	strncpy(newreq->rq_ind.rq_signal.rq_signame, signame, PBS_SIGNAMESZ);
	return (relay_to_mom(pjob, newreq, func));

	/* when MOM replies, we just free the request structure */
}

/**
 * @brief
 * 		post_signal_req - complete a Signal Job Request (externally generated)
 *
 * @param[in,out]	pwt	-	work_task which contains Signal Job Request
 */

void
post_signal_req(struct work_task *pwt)
{
	job *pjob;
	struct batch_request *preq;
	int rc;
	int ss;
	int suspend = 0;
	int resume = 0;

	if (pwt->wt_aux2 != PROT_TPP)
		svr_disconnect(pwt->wt_event);	/* disconnect from MOM */

	preq = pwt->wt_parm1;
	preq->rq_conn = preq->rq_orgconn;  /* restore client socket */
	pjob = preq->rq_extra;

	if(strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_SUSPEND)==0 ||
			strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_ADMIN_SUSPEND) == 0)
		suspend = 1;
	else if(strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_RESUME) == 0 ||
			strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_ADMIN_RESUME) == 0)
		resume = 1;

	if ((rc = preq->rq_reply.brp_code)) {

		/* there was an error on the Mom side of things */

		log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, LOG_DEBUG,
			preq->rq_ind.rq_signal.rq_jid, msg_momreject);
		errno = 0;
		if (rc == PBSE_UNKJOBID)
			rc = PBSE_INTERNAL;
		if (resume) {
			/* resume failed, re-release resc and nodes */
			rel_resc(pjob);
		}

		if (pjob == NULL)
			pjob = find_job(preq->rq_ind.rq_signal.rq_jid);
		if (pjob != NULL && pjob->ji_pmt_preq != NULL)
			reply_preempt_jobs_request(rc, PREEMPT_METHOD_SUSPEND, pjob);

		req_reject(rc, 0, preq);
	} else {

		/* everything went ok for signal request at Mom */

		if (suspend && pjob && (check_job_state(pjob, JOB_STATE_LTR_RUNNING))) {
			if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_Suspend) == 0) {
				if (preq->rq_fromsvr == 1 || pjob->ji_pmt_preq != NULL)
					ss = JOB_SUBSTATE_SCHSUSP;
				else
					ss = JOB_SUBSTATE_SUSPEND;
				if ((server.sv_attr[(int) SVR_ATR_restrict_res_to_release_on_suspend].at_flags & ATR_VFLAG_SET)) {
					if (create_resreleased(pjob) == 1) {
						sprintf(log_buffer, "Unable to create resource released list");
						log_event(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, LOG_INFO,
							  pjob->ji_qs.ji_jobid, log_buffer);
					}
				}
				pjob->ji_qs.ji_svrflags |= JOB_SVFLG_Suspend;
				/* update all released resources */
				svr_setjobstate(pjob, JOB_STATE_LTR_RUNNING, ss);
				rel_resc(pjob); /* release resc and nodes */
				job_save(pjob); /* save released resc and nodes */
				log_suspend_resume_record(pjob, PBS_ACCT_SUSPEND);
				/* Since our purpose is to put the node in maintenance state if "admin-suspend"
				 * signal is used, be sure that rel_resc() is called before set_admin_suspend().
				 * Otherwise, set_admin_suspend will move the node to maintenance state and
				 * rel_resc() will pull it out of maintenance state */
				if(strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_ADMIN_SUSPEND) == 0)
					set_admin_suspend(pjob, 1);

			}
		} else if (resume && pjob && (check_job_state(pjob, JOB_STATE_LTR_RUNNING))) {
			/* note - the resources have already been reallocated */
			pjob->ji_qs.ji_svrflags &= ~JOB_SVFLG_Suspend;
			if(strcmp(preq->rq_ind.rq_signal.rq_signame, SIG_ADMIN_RESUME) == 0)
				set_admin_suspend(pjob, 0);

			free_jattr(pjob, JOB_ATR_resc_released);
			mark_jattr_not_set(pjob, JOB_ATR_resc_released);

			free_jattr(pjob, JOB_ATR_resc_released_list);
			mark_jattr_not_set(pjob, JOB_ATR_resc_released_list);

			svr_setjobstate(pjob, JOB_STATE_LTR_RUNNING, JOB_SUBSTATE_RUNNING);
			log_suspend_resume_record(pjob, PBS_ACCT_RESUME);

			set_jattr_generic(pjob, JOB_ATR_Comment,
					form_attr_comment("Job run at %s", get_jattr_str(pjob,  JOB_ATR_exec_vnode)), NULL, SET);
		}

		if (pjob == NULL)
			pjob = find_job(preq->rq_ind.rq_signal.rq_jid);
		if (pjob != NULL && pjob->ji_pmt_preq != NULL)
			reply_preempt_jobs_request(PBSE_NONE, PREEMPT_METHOD_SUSPEND, pjob);

		reply_ack(preq);
	}
}


/**
 * @brief  Create the job's resources_released and Resource_Rel_List
 *	    attributes based on its exec_vnode
 * @param[in/out] pjob - Job structure
 *
 * @return int
 * @retval 1 - In case of failure
 * @retval 0 - In case of success
 */
int
create_resreleased(job *pjob)
{
	char *chunk;
	int j;
	int nelem;
	char *noden;
	int rc;
	struct key_value_pair *pkvp;
	char *resreleased;
	char buf[1024] = {0};
	char *dflt_ncpus_rel = ":ncpus=0";
	int no_res_rel = 1;

	attribute *pexech = get_jattr(pjob, JOB_ATR_exec_vnode);
	/* Multiplying by 2 to take care of superchunks of the format
	 * (node:resc=n+node:resc=m) which will get converted to
	 * (node:resc=n)+(node:resc=m). This will add room for this
	 * expansion.
	 */
	resreleased = (char *) calloc(1, strlen(pexech->at_val.at_str)*2 + 1);
	if (resreleased == NULL)
		return 1;
	resreleased[0] = '\0';

	chunk = parse_plus_spec(pexech->at_val.at_str, &rc);
	if (rc != 0) {
		free(resreleased);
		return 1;
	}
	while(chunk) {
		no_res_rel = 1;
		strcat(resreleased, "(");
		if (parse_node_resc(chunk, &noden, &nelem, &pkvp) == 0) {
			strcat(resreleased, noden);
			if (server.sv_attr[SVR_ATR_restrict_res_to_release_on_suspend].at_flags & ATR_VFLAG_SET) {
				for (j = 0; j < nelem; ++j) {
					int k;
					int np;
					np = server.sv_attr[SVR_ATR_restrict_res_to_release_on_suspend].at_val.at_arst->as_usedptr;
					for (k = 0; np != 0 && k < np; k++) {
						char *res;
						res = server.sv_attr[SVR_ATR_restrict_res_to_release_on_suspend].at_val.at_arst->as_string[k];
						if ((res != NULL) && (strcmp(pkvp[j].kv_keyw,res) == 0)) {
							sprintf(buf, ":%s=%s", res, pkvp[j].kv_val);
							strcat(resreleased, buf);
							no_res_rel = 0;
							break;
						}
					}
				}
			}
			else {
				free(resreleased);
				return 1;
			}
		} else {
			free(resreleased);
			return 1;
		}
		/* If there are no resources released on this vnode then add a dummy "ncpus=0"
		 * This is needed otherwise scheduler will not be able to assign this chunk to
		 * the job while trying to resume it
		 */
		if (no_res_rel)
			strcat(resreleased, dflt_ncpus_rel);
		strcat(resreleased, ")");
		chunk = parse_plus_spec(NULL, &rc);
		if (rc != 0) {
			free(resreleased);
			return 1;
		}
		if (chunk)
			strcat(resreleased, "+");
	}
	if (resreleased[0] != '\0')
		set_jattr_generic(pjob, JOB_ATR_resc_released, resreleased, NULL, SET);

	free(resreleased);
	return 0;
}


/**
 *	@brief Handle admin-suspend/admin-resume on the job and nodes
 *		set or remove the JOB_SVFLG_AdmSuspd flag on the job
 *		set or remove nodes in state maintenance
 *
 *	@param[in] pjob - job to act upon
 *	@param[in] set_remove_nstate if 1, set flag/state if 0 remove flag/state
 *
 *	@return void
 */
void set_admin_suspend(job *pjob, int set_remove_nstate) {
	char *chunk;
	char *execvncopy;
	char *last;
	char *vname;
	struct key_value_pair *pkvp;
	int hasprn;
	int nelem;
	struct pbsnode *pnode;
	attribute new;

	if(pjob == NULL)
		return;

	execvncopy = strdup(get_jattr_str(pjob, JOB_ATR_exec_vnode));

	if(execvncopy == NULL)
		return;

	if(set_remove_nstate)
		pjob->ji_qs.ji_svrflags |= JOB_SVFLG_AdmSuspd;
	 else
		pjob->ji_qs.ji_svrflags &= ~JOB_SVFLG_AdmSuspd;

	clear_attr(&new, &node_attr_def[(int)ND_ATR_MaintJobs]);
	decode_arst(&new, ATTR_NODE_MaintJobs, NULL, pjob->ji_qs.ji_jobid);

	chunk = parse_plus_spec_r(execvncopy, &last, &hasprn);

	while (chunk) {

		if (parse_node_resc(chunk, &vname, &nelem, &pkvp) == 0) {
			pnode = find_nodebyname(vname);
			if(pnode) {
				if(set_remove_nstate) {
					set_arst(&pnode->nd_attr[(int)ND_ATR_MaintJobs], &new, INCR);
					set_vnode_state(pnode, INUSE_MAINTENANCE, Nd_State_Or);
				} else {
					set_arst(&pnode->nd_attr[(int)ND_ATR_MaintJobs], &new, DECR);
					if (pnode->nd_attr[(int)ND_ATR_MaintJobs].at_val.at_arst->as_usedptr == 0)
						set_vnode_state(pnode, ~INUSE_MAINTENANCE, Nd_State_And);
				}
			}
		}
		chunk = parse_plus_spec_r(last, &last, &hasprn);
	}
	save_nodes_db(0, NULL);
	job_save_db(pjob);
	free_arst(&new);
	free(execvncopy);
}
