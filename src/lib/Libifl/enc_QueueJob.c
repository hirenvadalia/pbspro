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
 * @file	enc_QueueJob.c
 * @brief
 * encode_DIS_QueueJob() - encode a Queue Job Batch Request
 *
 *	This request is used for the first step in submitting a job, sending
 *	the job attributes.
 *
 * @par Data items are:
 * 			string	job id
 *			string	destination
 *			list of	attribute, see encode_DIS_attropl()
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "libpbs.h"
#include "pbs_error.h"

/**
 * @brief
 *	-encode a Queue Job Batch Request
 *
 * @par	Functionality:
 *		This request is used for the first step in submitting a job, sending
 *      	the job attributes.
 *
 * @param[in] sock - socket descriptor
 * @param[in] jobid - job id
 * @param[in] destin - destination queue name
 * @param[in] aoplp - pointer to attropl structure(list)
 * @return      int
 * @retval      DIS_SUCCESS(0)  success
 * @retval      error code      error
 *
 */

int
encode_DIS_QueueJob(int sock, char *jobid, char *destin, struct attropl *aoplp)
{
	int   rc;

	if (jobid == NULL)
		jobid = "";
	if (destin == NULL)
		destin = "";

	if ((rc = diswst(sock, jobid) != 0) ||
		(rc = diswst(sock, destin) != 0))
			return rc;

	return (encode_DIS_attropl(sock, aoplp));
}
