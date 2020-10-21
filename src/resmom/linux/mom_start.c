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

#include <pbs_config.h>   /* the master config generated by configure */
/**
 * @file
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <ftw.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/param.h>
#include <sys/utsname.h>
#include <ctype.h>
#include "libpbs.h"
#include "list_link.h"
#include "log.h"
#include "server_limits.h"
#include "attribute.h"
#include "resource.h"
#include "job.h"
#include "pbs_nodes.h"
#include "mom_mach.h"
#include "mom_func.h"
#ifdef PMIX
#include "mom_pmix.h"
#endif
#include "resmon.h"
#include "mom_vnode.h"
#include "libutil.h"
#include "work_task.h"

/**
 * @struct
 *	struct release_info is used to parse the release information
 *	of ProPack and linux distributions (i.e RHEL/SLES).
 *
 *	Keep the sgi-release file information at index 0
 *	To load the ProPack information, index 0 is used.
 *	To load the os information, index 1 thru end is used
 *	to search which file is available.
 */

static struct release_info {
	char *file;
	char *pfx;
	char *srch;
	char *sep;
} release_info[] = {
	{"/etc/redhat-release",	"RHEL",	"release",	" "},
	{"/etc/SuSE-release",	"SLES",	"VERSION",	"="},
	{"/etc/os-release",	"SLES",	"VERSION",	"="}
};

/**
 * @struct
 *	struct libjob_support is used to hold verified and tested list of
 *	<OS ver>, <Architecture>, <libjob ver>.
 */

static struct libjob_support {
	char *osver;
	char *arch;
	char *libjobver;
} libjob_support[] = {
	{"SLES10", "x86_64", "libjob.so"},
	{"SLES11", "x86_64", "libjob.so"},
	{"SLES12", "x86_64", "libjob.so.0"},
	{"SLES12", "aarch64", "libjob.so.0"},
	{"SLES15", "aarch64", "libjob.so.0"},
	{"SLES15", "x86_64", "libjob.so.0"}
};

/* Global Variables */

extern	int		exiting_tasks;
extern	char		mom_host[];
extern	pbs_list_head	svr_alljobs;
extern	int		termin_child;
extern	int		num_acpus;
extern	int		num_pcpus;
extern	int		svr_delay_entry;

extern	pbs_list_head	task_list_event;

#if	MOM_ALPS
extern	char		*path_jobs;
char *get_versioned_libname();
int find_in_lib(void *handle, char * plnam, char *psnam, void ** psp);

/**
 *	This is a temporary kludge - this work should really be done by
 *	pbs_sched:  if the job is getting exclusive use of a vnode, we
 *	will assign all the CPU
 *	resources of the vnode to the created CPU set.  Exclusive use of
 *	a vnode is defined by a table in (currently) section E16.4 of the
 *	GRUNT 2 document, q.v..  It is reproduced here
 *
 *					Resource_List.place value
 *	vnode "sharing"
 *	   value		unset	   contains "share"   contains "excl"
 *			   ---------------------------------------------------|
 * 	unset  	       	   |   	share  	  |    	share  	  |    	excl   	      |
 *     	       	       	   |--------------|---------------|-------------------|
 *	"default_shared"   |	share	  |	share	  |	excl	      |
 *		    	   |--------------|---------------|-------------------|
 *	"default_excl"	   |	excl	  |	share	  |	excl	      |
 *	       	    	   |--------------|---------------|-------------------|
 *	"ignore_excl"	   |	share	  |	share	  |	share	      |
 *		    	   |--------------|---------------|-------------------|
 *	"force_excl"	   |	excl	  |	excl	  |	excl 	      |
 *			   |---------------------------------------------------
 *
 *	and reflected in the vnss[][] array below.
 *
 *	This applies to ALPS because the Cray reservation has an EXCLUSIVE
 *	or SHARED mode that is set from this table.
 */
enum vnode_sharing_state vnss[][rlplace_excl - rlplace_unset + 1] = {
	{ isshared,	isshared,	isexcl },	/* VNS_UNSET */
	{ isshared,	isshared,	isexcl },	/* VNS_DFLT_SHARED */
	{ isexcl,	isshared,	isexcl },	/* VNS_DFLT_EXCL */
	{ isshared,	isshared,	isshared },	/* VNS_IGNORE_EXCL */
	{ isexcl,	isexcl,		isexcl },	/* VNS_FORCE_EXCL */
	{ isexcl,	isshared,	isexcl },	/* VNS_DFLT_EXCLHOST */
	{ isexcl,	isexcl,		isexcl }	/* VNS_FORCE_EXCLHOST */
};

#ifndef MAX
#define MAX(a, b) (((a)>(b))?(a):(b))
#endif

/**
 * @brief
 *   	getplacesharing	sharing value for job place
 *
 *   	Compare the "place" string for a job with "excl" and "share" and
 *   	return the corresponding rlplace_value.
 *
 * @param[in] 	pjob	the job of interest
 *
 * @return	enum rlplace_value
 *
 * @par Side-effects
 *   	A log message is printed at DEBUG level.
 *
 * @par
 *   	This code was put in an externally
 *   	available function for use by the Cray project.
 *
 */
enum rlplace_value
getplacesharing(job *pjob)
{
	static	resource_def	*prsdef = NULL;
	enum rlplace_value	rpv = rlplace_unset;
	resource		*pplace;

	/*
	 *	Compute the "Resource_List.place" index for vnss[][]:
	 */
	prsdef = &svr_resc_def[RESC_PLACE];
	if (prsdef != NULL) {
		char	*placeval = NULL;

		pplace = find_resc_entry(get_jattr(pjob, JOB_ATR_resource), prsdef);
		if (pplace)
			placeval = pplace->rs_value.at_val.at_str;
		if (placeval != NULL) {
			if (place_sharing_check(placeval, PLACE_Excl))
				rpv = rlplace_excl;
			else if (place_sharing_check(placeval, PLACE_ExclHost))
				rpv = rlplace_excl;
			else if (place_sharing_check(placeval, PLACE_Shared))
				rpv = rlplace_share;

			sprintf(log_buffer, "Resource_List.place = %s",
				placeval);
			log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB,
				LOG_DEBUG, pjob->ji_qs.ji_jobid,
				log_buffer);
		}
	}
	return rpv;
}

/* These globals are initialized in ck_acct_facility_present.
 *
 * At a later time it may be better to relocate them to the machine
 * independent portion of the mom code if they find use by more
 * than a single machine/OS type
 */

int job_facility_present;
int job_facility_enabled;
int acct_facility_present;
int acct_facility_active;
jid_t	(*jc_create)();
jid_t	(*jc_getjid)();

void
ck_acct_facility_present(void)
{
	int	ret1;
	int	ret2;
	char	*libjob;

	static void *handle1 = NULL;

	struct	config		*cptr;
	extern	struct	config	*config_array;

	/* use of job_create defaults to True */
	job_facility_enabled = 1;

	for (cptr = config_array; cptr != NULL; cptr++) {
		if (cptr->c_name == NULL || *cptr->c_name == 0)
			break;
		else if (strcasecmp(cptr->c_name, "pbs_jobcreate_workload_mgmt") == 0) {
			(void)set_boolean(__func__, cptr->c_u.c_value,
				&job_facility_enabled);
		}
	}

	/* multiple calls to dlopen with the same arguments do not cause multiple
	 * copies of the library to get loaded into the proesses memory, they just
	 * bump a reference count and return the same handle value.
	 * If dlclose is issued when the reference count is 1, the library will be
	 * unloaded from memory and any previous pointers obtained through calls to
	 * dlsym will not be valid.
	 */

	job_facility_present = 0;
	acct_facility_present = 0;
	acct_facility_active = 0;

	/*
	 * If job facility is turned off, don't call dlopen for job_create.
	 */
	if (job_facility_enabled == 0)
		goto done;

	libjob = get_versioned_libname();
	if (libjob == NULL) {
		sprintf(log_buffer, "Could not find a supported job shared object");
		log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_ACCT, LOG_DEBUG, __func__,
				log_buffer);
		goto err;
	}

	sprintf(log_buffer, "using %s for job shared object", libjob);
	log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_ACCT, LOG_DEBUG, __func__, log_buffer);
	handle1 = dlopen(libjob, RTLD_LAZY);
	if (handle1 == NULL) {
		/* facility is not available */

		sprintf(log_buffer, "%s. failed to dlopen %s", dlerror(), libjob);
		log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_ACCT, LOG_DEBUG,
			__func__, log_buffer);
		goto err;
	}

	sprintf(log_buffer, "dlopen of %s successful", libjob);
	log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_ACCT, LOG_DEBUG,
			__func__, log_buffer);

	/* find_in_lib sets message in log_buffer */
	ret1 = find_in_lib(handle1, libjob, "job_create", (void **)&jc_create);
	log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_ACCT, LOG_DEBUG, __func__,
			log_buffer);

	ret2 = find_in_lib(handle1, libjob, "job_getjid", (void **)&jc_getjid);
	log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_ACCT, LOG_DEBUG, __func__,
			log_buffer);

	if ((ret1 == 1) && (ret2 == 1))
		job_facility_present = 1;

	if (job_facility_present == 0)
		goto done;

err:
	log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_ACCT, LOG_DEBUG,
			__func__, "job facility not present or improperly setup");

done:
	/*
	 * When we get here, the flags are set to indicate what libs should
	 * be kept open.
	 */
	if (job_facility_present == 0) {
		if (handle1) {
			dlclose(handle1);
			handle1 = NULL;
		}
	}
}

/**
 * @brief
 *	find_in_lib -  Call this function when you want to find the address of symbol
 * 	in a shared library that has been opened by a call to dlopen.
 *
 * 	An appropriate message will be written into PBS' global "log_buffer"
 * 	in each of the three possible cases (found, not found, bogus arguments).
 * 	The caller chooses to log or ignore the content of log_buffer.
 *
 *
 * @param[in]	handle	valid handle from call to dlopen
 * @param[in]	plnam	pointer to the name of the library (NULL acceptable)
 * @param[in]	psnam	pointer to the name of the symbol
 * @param[out]	psp	where to return the symbol pointer if found
 *
 * @return	int
 * @retval	1      success, with symbol pointer stored to *psp
 * @retval 	0      failure, and *psp unmodified
 * @retval	-1      bad input to this function
 *
 */
int
find_in_lib(void *handle, char * plnam, char *psnam, void ** psp)
{
	void		*psym;
	const char	*error;
	int		retcode;

	/* check arguments */
	if (handle == NULL || psnam == NULL || *psnam == '\0') {
		sprintf(log_buffer, "%s: bad arguments %p %p %p %p", __func__,
			handle, plnam, psnam, psp);
		return -1;
	}

	psym = dlsym(handle, psnam);
	error = dlerror();

	if (error != NULL) {

		retcode = 0;
		if (plnam)
			sprintf(log_buffer, "%s. symbol %s not found in %s", error, psnam, plnam);
		else
			sprintf(log_buffer, "%s. symbol %s not found", error, psnam);
	} else {

		retcode = 1;
		*psp = psym;

		if (plnam)
			sprintf(log_buffer, "symbol %s found in %s", psnam, plnam);
		else
			sprintf(log_buffer, "symbol %s found", psnam);
	}
	return (retcode);
}

#endif /* MOM_ALPS */


/* Private variables */

/**
 * @brief
 * 	Set session id and whatever else is required on this machine
 *	to create a new job.
 * 	On a Cray, an ALPS reservation will be created and confirmed.
 *
 * @param[in]	pjob	-	pointer to job structure
 * @param[in]	sjr	-	pointer to startjob_rtn structure
 *
 * @return session/job id
 * @retval -1 error from setsid(), no message in log_buffer
 * @retval -2 temporary error, retry job, message in log_buffer
 * @retval -3 permanent error, abort job, message in log_buffer
 *
 */
int
set_job(job *pjob, struct startjob_rtn *sjr)
{
#if	MOM_ALPS
	if (job_facility_present && pjob->ji_qs.ji_svrflags & JOB_SVFLG_HERE) {

		/* host system has necessary JOB container facility present
		 * and this host is Mother Superior for this job
		 */

		jid_t *pjid = (jid_t *) &pjob->ji_extended.ji_ext.ji_jid[0];

		if (*pjid != (jid_t)0 && *pjid != (jid_t)-1) {
			sjr->sj_jid = *pjid;
		} else {

			errno = -1;
			sjr->sj_jid = (jc_create == NULL) ? -1 :
				(*jc_create)(0, pjob->ji_qs.ji_un.ji_momt.ji_exuid, 0);

			if (sjr->sj_jid == (jid_t)-1) {

				/* Failed: categorize errno into two cases and handle */
				/* Remark: sit_job call occurs before log_close()     */

				if (errno == ENOSYS) {
					if (job_facility_present == 1) {
						log_joberr(errno, __func__,
							"Job container facility unavailable",
							pjob->ji_qs.ji_jobid);
						job_facility_present = 0;
					}
				} else {

					/* log any other job_create failure type */

					log_joberr(errno, __func__,
						"Job container job_create call failed", pjob->ji_qs.ji_jobid);
				}
			}
		}

		*pjid = sjr->sj_jid;
	}
#endif	/* MOM_ALPS */

	sjr->sj_session = setsid();

#if	MOM_ALPS
	/*
	 * Now that we have our SID/JID we can request/confirm our
	 * placement scheduler reservation.
	 *
	 * Do this only if we are mother superior for the job.
	 */

	if (pjob->ji_qs.ji_svrflags & JOB_SVFLG_HERE) {
		basil_request_reserve_t *basil_req;
		int rc;

		/* initialized to -1 so this catches the unset case. */
		sjr->sj_reservation = -1;

		rc = alps_create_reserve_request(pjob, &basil_req);
		if (rc == 1) {
			sprintf(log_buffer,
				"Fatal MPP reservation error"
				" preparing request.");
			return -3;
		} else if (rc == 2) {
			sprintf(log_buffer,
				"Transient MPP reservation error"
				" preparing request.");
			return -2;
		}
		if (basil_req) {
			rc = alps_create_reservation(basil_req,
				&sjr->sj_reservation,
				&sjr->sj_pagg);
			alps_free_reserve_request(basil_req);
			if (rc < 0) {
				sprintf(log_buffer,
					"Fatal MPP reservation error"
					" on create.");
				return -3;
			}
			if (rc > 0) {
				sprintf(log_buffer,
					"Transient MPP reservation error"
					" on create.");
				return -2;
			}
			/*
			 * If we are interacting with ALPS, the cookie has
			 * not been set. Fill in the session ID we just
			 * acquired. Otherwise, we are interacting with
			 * CPA and use the cookie that was acquired when
			 * the reservation was created.
			 */
			if (sjr->sj_pagg == 0) {
				if ((job_facility_present == 1))
					sjr->sj_pagg = sjr->sj_jid;
				else
					sjr->sj_pagg = sjr->sj_session;
			}
			pjob->ji_extended.ji_ext.ji_reservation =
				sjr->sj_reservation;
			pjob->ji_extended.ji_ext.ji_pagg =
				sjr->sj_pagg;

			rc = alps_confirm_reservation(pjob);
			if (rc < 0) {
				sprintf(log_buffer,
					"Fatal MPP reservation error"
					" on confirm.");
				return -3;
			}
			if (rc > 0) {
				sprintf(log_buffer,
					"Transient MPP reservation error"
					" on confirm.");
				return -2;
			}
		} else {	/* No error but no reservation made, reset so
					 * the inventory will not be reread.
					 */
			sjr->sj_reservation = 0;
		}
	}
#endif	/* MOM_ALPS */

	return (sjr->sj_session);
}

/**
 * @brief
 *	set_globid - set the global id for a machine type.
 *
 * @param[in] pjob - pointer to job structure
 * @param[in] sjr  - pointer to startjob_rtn structure
 *
 * @return Void
 *
 */

void
set_globid(job *pjob, struct startjob_rtn *sjr)
{
#if	MOM_ALPS
	char buf[19];  /* 0x,16 hex digits,'\0' */
	char altid_buf[23];

	if (sjr->sj_jid == (jid_t)-1)
		job_facility_present = 0;
	else if (sjr->sj_jid) {

		sprintf(buf, "%#0lx", (unsigned long)sjr->sj_jid);
		set_jattr_str_slim(pjob, JOB_ATR_acct_id, buf, NULL);
		(void)memcpy(&pjob->ji_extended.ji_ext.ji_jid, &sjr->sj_jid, sizeof(pjob->ji_extended.ji_ext.ji_jid));

		if (job_facility_present == 0) {
			/* first success on job_create() after failure */
			job_facility_present = 1;
			sprintf(log_buffer, "Job container facility available");
			log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_ACCT, LOG_DEBUG, __func__, log_buffer);
		}
	}

	pjob->ji_extended.ji_ext.ji_pagg = sjr->sj_pagg;
	pjob->ji_extended.ji_ext.ji_reservation = sjr->sj_reservation;
	sprintf(altid_buf, "%ld", sjr->sj_reservation);
	set_jattr_str_slim(pjob, JOB_ATR_altid, altid_buf, NULL);

#endif	/* MOM_ALPS */
}

/**
 * @brief
 *	sets the shell to be used
 *
 * @param[in] pjob - pointer to job structure
 * @param[in] pwdp - pointer to passwd structure
 *
 * @return 	string
 * @retval 	shellname	Success
 *
 */
char *
set_shell(job *pjob, struct passwd *pwdp)
{
	char *cp;
	int   i;
	char *shell;
	struct array_strings *vstrs;
	/*
	 * find which shell to use, one specified or the login shell
	 */

	shell = pwdp->pw_shell;
	if ((is_jattr_set(pjob, JOB_ATR_shell)) &&
		(vstrs = get_jattr_arst(pjob, JOB_ATR_shell))) {
		for (i = 0; i < vstrs->as_usedptr; ++i) {
			cp = strchr(vstrs->as_string[i], '@');
			if (cp) {
				if (!strncmp(mom_host, cp+1, strlen(cp+1))) {
					*cp = '\0';	/* host name matches */
					shell = vstrs->as_string[i];
					break;
				}
			} else {
				shell = vstrs->as_string[i];	/* wildcard */
			}
		}
	}
	return (shell);
}

/**
 *
 * @brief
 * 	Checks if a child of the current (mom) process has terminated, and
 *	matches it with the pid of one of the tasks in the task_list_event,
 *	or matches the pid of a process being monitored for a PBS job.
 *	if matching a task in the task_list_event, then that task is
 *	marked as WORK_Deferred_Cmp along with the exit value of the child
 *	process. Otherwise if it's for a job, and that job's
 *	JOB_SVFLAG_TERMJOB is set, then mark the job as exiting.
 *
 * @return	Void
 *
 */

void
scan_for_terminated(void)
{
	int		exiteval;
	pid_t		pid;
	job		*pjob;
	task		*ptask = NULL;
	struct work_task *wtask = NULL;
	int		statloc;

	/* update the latest intelligence about the running jobs;         */
	/* must be done before we reap the zombies, else we lose the info */

	termin_child = 0;

	mom_set_use_all();

	/* Now figure out which task(s) have terminated (are zombies) */

	while ((pid = waitpid(-1, &statloc, WNOHANG)) > 0) {
		if (WIFEXITED(statloc))
			exiteval = WEXITSTATUS(statloc);
		else if (WIFSIGNALED(statloc))
			exiteval = WTERMSIG(statloc) + 0x100;
		else
			exiteval = 1;


		/* Check for other task lists */
		wtask = (struct work_task *)GET_NEXT(task_list_event);
		while (wtask) {
			if ((wtask->wt_type == WORK_Deferred_Child) &&
				(wtask->wt_event == pid)) {
				wtask->wt_type = WORK_Deferred_Cmp;
				wtask->wt_aux = (int)exiteval; /* exit status */
				svr_delay_entry++;	/* see next_task() */
			}
			wtask = (struct work_task *)GET_NEXT(wtask->wt_linkall);
		}

		pjob = (job *)GET_NEXT(svr_alljobs);
		while (pjob) {
			/*
			 ** see if process was a child doing a special
			 ** function for MOM
			 */
			if (pid == pjob->ji_momsubt)
				break;
			/*
			 ** look for task
			 */
			ptask = (task *)GET_NEXT(pjob->ji_tasks);
			while (ptask) {
				if (ptask->ti_qs.ti_sid == pid)
					break;
				ptask = (task *)GET_NEXT(ptask->ti_jobtask);
			}
			if (ptask != NULL)
				break;
			pjob = (job *)GET_NEXT(pjob->ji_alljobs);
		}

		if (pjob == NULL) {
			DBPRT(("%s: pid %d not tracked, exit %d\n",
				__func__, pid, exiteval))
			continue;
		}

		if (pid == pjob->ji_momsubt) {
			pjob->ji_momsubt = 0;
			if (pjob->ji_mompost) {
				pjob->ji_mompost(pjob, exiteval);
			}
			(void)job_save(pjob);
			continue;
		}
		DBPRT(("%s: task %8.8X pid %d exit value %d\n", __func__,
			ptask->ti_qs.ti_task, pid, exiteval))
		ptask->ti_qs.ti_exitstat = exiteval;
		sprintf(log_buffer, "task %8.8X terminated",
			ptask->ti_qs.ti_task);
		log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG,
			pjob->ji_qs.ji_jobid, log_buffer);

#ifdef PMIX
		/* Inform PMIx that the task has exited. */
		pbs_pmix_notify_exit(pjob, ptask->ti_qs.ti_exitstat, NULL);
#endif

		/*
		 ** After the top process(shell) of the TASK exits, check if the
		 ** JOB_SVFLG_TERMJOB job flag set. If yes, then check for any
		 ** live process(s) in the session. If found, make the task
		 ** ORPHAN by setting the flag and delay by kill_delay time. This
		 ** will be exited in kill_job or by cput_sum() as can not be
		 ** seen again by scan_for_terminated().
		 */
		if (pjob->ji_qs.ji_svrflags & JOB_SVFLG_TERMJOB) {
			int	n;

			(void)mom_get_sample();
			n = bld_ptree(ptask->ti_qs.ti_sid);
			if (n > 0) {
				ptask->ti_flags |= TI_FLAGS_ORPHAN;
				DBPRT(("%s: task %8.8X still has %d active procs\n", __func__,
					ptask->ti_qs.ti_task, n))
				continue;
			}
		}

		kill_session(ptask->ti_qs.ti_sid, SIGKILL, 0);
		ptask->ti_qs.ti_status = TI_STATE_EXITED;
		(void)task_save(ptask);
		exiting_tasks = 1;
	}
}


#ifdef	HAVE_POSIX_OPENPT

/**
 * @brief
 *	This is code adapted from an example for posix_openpt in
 *	The Open Group Base Specifications Issue 6.
 *
 *	On success, this function returns an open descriptor for the
 *	master pseudotty and places a pointer to the (static) name of
 *	the slave pseudotty in *rtn_name;  on failure, -1 is returned.
 *
 * @param[out] rtn_name - holds info of tty
 *
 * @return 	int
 * @retval 	fd 	Success
 * @retval 	-1	Failure
 *
 */
int
open_master(char **rtn_name)
{
	int		masterfd;
	char		*newslavename;
	static char	slavename[_POSIX_PATH_MAX];
#ifndef	_XOPEN_SOURCE
	extern char	*ptsname(int);
	extern int	grantpt(int);
	extern int	unlockpt(int);
	extern int	posix_openpt(int);
#endif

	masterfd = posix_openpt(O_RDWR | O_NOCTTY);
	if (masterfd == -1)
		return (-1);

	if ((grantpt(masterfd) == -1) ||
		(unlockpt(masterfd) == -1) ||
		((newslavename = ptsname(masterfd)) == NULL)) {
		(void) close(masterfd);
		return (-1);
	}

	pbs_strncpy(slavename, newslavename, sizeof(slavename));
	assert(rtn_name != NULL);
	*rtn_name = slavename;
	return (masterfd);
}

#else	/* HAVE_POSIX_OPENPT */

/**
 * @brief
 * 	creat the master pty, this particular
 * 	piece of code depends on multiplexor /dev/ptc
 *
 * @param[in] rtn_name - holds info about tty
 * @return      int
 * @retval      fd      Success
 * @retval      -1      Failure
 *
 */

#define PTY_SIZE 12

int
open_master(char **rtn_name)
{
	char 	       *pc1;
	char 	       *pc2;
	int		ptc;	/* master file descriptor */
	static char	ptcchar1[] = "pqrs";
	static char	ptcchar2[] = "0123456789abcdef";
	static char	pty_name[PTY_SIZE+1];	/* "/dev/[pt]tyXY" */

	pbs_strncpy(pty_name, "/dev/ptyXY", sizeof(pty_name));
	for (pc1 = ptcchar1; *pc1 != '\0'; ++pc1) {
		pty_name[8] = *pc1;
		for (pc2 = ptcchar2; *pc2 != '\0'; ++pc2) {
			pty_name[9] = *pc2;
			if ((ptc = open(pty_name, O_RDWR | O_NOCTTY, 0)) >= 0) {
				/* Got a master, fix name to matching slave */
				pty_name[5] = 't';
				*rtn_name = pty_name;
				return (ptc);

			} else if (errno == ENOENT)
				return (-1);	/* tried all entries, give up */
		}
	}
	return (-1);	/* tried all entries, give up */
}
#endif	/* HAVE_POSIX_OPENPT */


/*
 * struct sig_tbl = map of signal names to numbers,
 * see req_signal() in ../requests.c
 */
struct sig_tbl sig_tbl[] = {
	{ "NULL", 0 },
	{ "HUP", SIGHUP },
	{ "INT", SIGINT },
	{ "QUIT", SIGQUIT },
	{ "ILL",  SIGILL },
	{ "TRAP", SIGTRAP },
	{ "IOT", SIGIOT },
	{ "ABRT", SIGABRT },
	{ "FPE", SIGFPE },
	{ "KILL", SIGKILL },
	{ "BUS", SIGBUS },
	{ "SEGV", SIGSEGV },
	{ "PIPE", SIGPIPE },
	{ "ALRM", SIGALRM },
	{ "TERM", SIGTERM },
	{ "URG", SIGURG },
	{ "STOP", SIGSTOP },
	{ "TSTP", SIGTSTP },
	{ "CONT", SIGCONT },
	{ "CHLD", SIGCHLD },
	{ "CLD",  SIGCHLD },
	{ "TTIN", SIGTTIN },
	{ "TTOU", SIGTTOU },
	{ "IO", SIGIO },
#ifdef __linux__
	{ "POLL", SIGPOLL },
#endif
	{ "XCPU", SIGXCPU },
	{ "XFSZ", SIGXFSZ },
	{ "VTALRM", SIGVTALRM },
	{ "PROF", SIGPROF },
	{ "WINCH", SIGWINCH },
	{ "USR1", SIGUSR1 },
	{ "USR2", SIGUSR2 },
	{NULL, -1 }
};

/**
 * @brief
 *      Get the release information
 *
 * @par Functionality:
 *      This function extracts the release information of ProPack and Linux distributions
 *      from system files listed in struct release_info.
 *
 * @see
 *      get_versioned_lib
 *
 * @param[in]   file    -       pointer to file
 * @param[in]   pfx     -       pointer to prefix
 * @param[in]   srch    -       pointer to search string
 * @param[in]   sep     -       pointer to separator
 *
 * @return	char *
 * @retval	distro: <PP ver> or <OS ver>
 * @retval	NULL: Not able to get the requested information from distro
 *
 * @par Side Effects: The value returned needs to be freed by the caller.
 *
 * @par MT-safe: Yes
 *
 */

static char *
parse_sysfile_info(const char *file,
	const char *pfx,
	const char *srch,
	const char *sep)
{
	FILE *fptr;
	char rbuf[1024];
	char *tok;
	char *svptr = NULL;
	char *distro;
	int found = 0;

	fptr = fopen(file, "r");
	if (fptr == NULL)
		return NULL;

	while (fgets(rbuf, sizeof(rbuf), fptr) != NULL) {
		if (strstr(rbuf, srch)) {
			found = 1;
			break;
		}
	}

	fclose(fptr);

	if (found == 0) {
		sprintf(log_buffer, "release info not found in %s", file);
		log_err(errno, __func__, log_buffer);
		return NULL;
	}

	tok = string_token(rbuf, sep, &svptr);
	while (tok) {
		if (strstr(tok, srch)) {
			tok = string_token(NULL, sep, &svptr);
			break;
		}
		tok = string_token(NULL, sep, &svptr);
	}
	if (tok == NULL)
		return NULL;

	while (!isdigit((int)(*tok)))
		tok++;
	distro = malloc(MAXNAMLEN);
	if (distro == NULL) {
		sprintf(log_buffer, "memory allocation failed");
		log_err(errno, __func__, log_buffer);
		return NULL;
	}
	(void)snprintf(distro, MAXNAMLEN, "%s%d", pfx, atoi(tok));
	distro[MAXNAMLEN - 1] = '\0';
	return distro;
}

/**
 *@brief
 *	Ensure that the shared object exists and
 *	get the shared object name from the table
 *
 * @par Functionality:
 *	This function checks verified and tested list of
 *	<PP ver>, <OS ver>, <Architecture>  and
 *	if the above entries matches with  libjob_support table,
 *	then returns shared object to the caller for dlopen.
 *	Otherwise it returns NULL.
 *
 * @see
 *	ck_acct_facility_present
 *
 * @return	char *
 * @retval	libjob_support[idx].libjobver
 * @retval	NULL:				Failed to get the supported library
 *
 * @par Side Effects: None
 *
 * @par MT-safe: Yes
 *
 */

char *
get_versioned_libname()
{
	int    idx;
	int    table_size;
	struct utsname buf;
	struct libjob_support jobobj;

	memset(&jobobj, 0, sizeof(jobobj));

	/* find OS information - loop to find out which file available */
	table_size = sizeof(release_info)/sizeof(release_info[0]);
	for (idx = 1; idx < table_size; idx++) {
		if (access(release_info[idx].file, R_OK) != -1)
			break;
	}

	/* if we found a readable os release file, parse it.
	 * if we dont find a file or if parse_sysfile_info fails,
	 * jobobj.osver remains NULL, and is handled later
	 */
	if (idx < table_size)
		jobobj.osver = parse_sysfile_info(release_info[idx].file,
			release_info[idx].pfx,
			release_info[idx].srch,
			release_info[idx].sep);
	/* Get the information on architecture */
	if (uname(&buf) == -1) {
		sprintf(log_buffer, "uname() call failed");
		log_err(errno, __func__, log_buffer);
		goto SYSFAIL;
	}

	jobobj.arch = strdup(buf.machine);

	/* check that all the required members of jobobj are NON-NULL */
	if ((jobobj.arch == NULL) || (jobobj.osver == NULL)) {
		sprintf(log_buffer, "Failed to get system information");
		log_err(errno, __func__, log_buffer);
		goto SYSFAIL;
	}

	/* Compare system information with verified list of platforms */

	table_size = sizeof(libjob_support)/sizeof(libjob_support[0]);
	for (idx = 0; idx < table_size; idx++) {
		if ((strcmp(jobobj.osver, libjob_support[idx].osver) == 0) &&
			(strcmp(jobobj.arch, libjob_support[idx].arch) == 0)) {
			free(jobobj.arch);
			free(jobobj.osver);
			return libjob_support[idx].libjobver;
		}
	}

SYSFAIL:
	if (jobobj.arch)
		free(jobobj.arch);
	if (jobobj.osver)
		free(jobobj.osver);
	return NULL;
}
