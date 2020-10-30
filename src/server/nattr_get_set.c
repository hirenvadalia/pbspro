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

#include "pbs_nodes.h"

attribute *
get_nattr(const struct pbsnode *pnode, int attr_idx)
{
	if (pnode != NULL)
		return get_attr_generic((attribute *)pnode->nd_attr, attr_idx);
	return NULL;
}

/**
 * @brief	Getter function for job attribute of type string
 *
 * @param[in]	pnode - pointer to the job
 * @param[in]	attr_idx - index of the attribute to return
 *
 * @return	char *
 * @retval	string value of the attribute
 * @retval	NULL if pnode is NULL
 */
char *
get_nattr_str(const struct pbsnode *pnode, int attr_idx)
{
	if (pnode != NULL)
		return get_attr_str(get_nattr(pnode, attr_idx));

	return NULL;
}

/**
 * @brief	Getter function for job attribute of type string of array
 *
 * @param[in]	pnode - pointer to the job
 * @param[in]	attr_idx - index of the attribute to return
 *
 * @return	struct array_strings *
 * @retval	value of the attribute
 * @retval	NULL if pnode is NULL
 */
struct array_strings *
get_nattr_arst(const struct pbsnode *pnode, int attr_idx)
{
	if (pnode != NULL)
		return get_attr_arst(get_nattr(pnode, attr_idx));

	return NULL;
}

/**
 * @brief	Getter for job attribute's list value
 *
 * @param[in]	pnode - pointer to the job
 * @param[in]	attr_idx - index of the attribute to return
 *
 * @return	pbs_list_head
 * @retval	value of attribute
 */
pbs_list_head
get_nattr_list(const struct pbsnode *pnode, int attr_idx)
{
	return get_attr_list(get_nattr(pnode, attr_idx));
}

/**
 * @brief	Getter function for job attribute of type long
 *
 * @param[in]	pnode - pointer to the job
 * @param[in]	attr_idx - index of the attribute to return
 *
 * @return	long
 * @retval	long value of the attribute
 * @retval	-1 if pnode is NULL
 */
long
get_nattr_long(const struct pbsnode *pnode, int attr_idx)
{
	if (pnode != NULL)
		return get_attr_l(get_nattr(pnode, attr_idx));

	return -1;
}

char
get_nattr_c(const struct pbsnode *pnode, int attr_idx)
{
	if (pnode != NULL)
		return get_attr_c(get_nattr(pnode, attr_idx));

	return -1;
}

/**
 * @brief	Generic Job attribute setter (call if you want at_set() action functions to be called)
 *
 * @param[in]	pnode - pointer to job
 * @param[in]	attr_idx - attribute index to set
 * @param[in]	val - new val to set
 * @param[in]	rscn - new resource val to set, if applicable
 * @param[in]	op - batch_op operation, SET, INCR, DECR etc.
 *
 * @return	int
 * @retval	0 for success
 * @retval	1 for failure
 */
int
set_nattr_generic(struct pbsnode *pnode, int attr_idx, char *val, char *rscn, enum batch_op op)
{
	if (pnode == NULL || val == NULL)
		return 1;

	return set_attr_generic(get_nattr(pnode, attr_idx), &node_attr_def[attr_idx], val, rscn, op);
}

/**
 * @brief	"fast" job attribute setter for string values
 *
 * @param[in]	pnode - pointer to job
 * @param[in]	attr_idx - attribute index to set
 * @param[in]	val - new val to set
 * @param[in]	rscn - new resource val to set, if applicable
 *
 * @return	int
 * @retval	0 for success
 * @retval	1 for failure
 */
int
set_nattr_str_slim(struct pbsnode *pnode, int attr_idx, char *val, char *rscn)
{
	if (pnode == NULL || val == NULL)
		return 1;

	return set_attr_generic(get_nattr(pnode, attr_idx), &node_attr_def[attr_idx], val, rscn, INTERNAL);
}

/**
 * @brief	"fast" job attribute setter for long values
 *
 * @param[in]	pnode - pointer to job
 * @param[in]	attr_idx - attribute index to set
 * @param[in]	val - new val to set
 * @param[in]	op - batch_op operation, SET, INCR, DECR etc.
 *
 * @return	int
 * @retval	0 for success
 * @retval	1 for failure
 */
int
set_nattr_l_slim(struct pbsnode *pnode, int attr_idx, long val, enum batch_op op)
{
	if (pnode == NULL)
		return 1;

	set_attr_l(get_nattr(pnode, attr_idx), val, op);

	return 0;
}

/**
 * @brief	"fast" job attribute setter for boolean values
 *
 * @param[in]	pnode - pointer to job
 * @param[in]	attr_idx - attribute index to set
 * @param[in]	val - new val to set
 * @param[in]	op - batch_op operation, SET, INCR, DECR etc.
 *
 * @return	int
 * @retval	0 for success
 * @retval	1 for failure
 */
int
set_nattr_b_slim(struct pbsnode *pnode, int attr_idx, long val, enum batch_op op)
{
	if (pnode == NULL)
		return 1;

	set_attr_b(get_nattr(pnode, attr_idx), val, op);

	return 0;
}

/**
 * @brief	"fast" job attribute setter for char values
 *
 * @param[in]	pnode - pointer to job
 * @param[in]	attr_idx - attribute index to set
 * @param[in]	val - new val to set
 * @param[in]	op - batch_op operation, SET, INCR, DECR etc.
 *
 * @return	int
 * @retval	0 for success
 * @retval	1 for failure
 */
int
set_nattr_c_slim(struct pbsnode *pnode, int attr_idx, char val, enum batch_op op)
{
	if (pnode == NULL)
		return 1;

	set_attr_c(get_nattr(pnode, attr_idx), val, op);

	return 0;
}

int
set_nattr_short_slim(struct pbsnode *pnode, int attr_idx, short val, enum batch_op op)
{
	if (pnode == NULL)
		return 1;

	set_attr_short(get_nattr(pnode, attr_idx), val, op);

	return 0;
}


/**
 * @brief	Check if a job attribute is set
 *
 * @param[in]	pnode - pointer to job
 * @param[in]	attr_idx - attribute index to check
 *
 * @return	int
 * @retval	1 if it is set
 * @retval	0 otherwise
 */
int
is_nattr_set(const struct pbsnode *pnode, int attr_idx)
{
	if (pnode != NULL)
		return is_attr_set(get_nattr(pnode, attr_idx));

	return 0;
}

/**
 * @brief	Free a job attribute
 *
 * @param[in]	pnode - pointer to job
 * @param[in]	attr_idx - attribute index to free
 *
 * @return	void
 */
void
free_nattr(struct pbsnode *pnode, int attr_idx)
{
	if (pnode != NULL)
		free_attr_generic(node_attr_def, get_nattr(pnode, attr_idx), attr_idx);
}

void
clear_nattr(struct pbsnode *pnode, int attr_idx)
{
	clear_attr(get_nattr(pnode, attr_idx), &node_attr_def[attr_idx]);
}

void
set_nattr_jinfo(struct pbsnode *pnode, int attr_idx, struct pbsnode *val)
{
	attribute *attr = get_nattr(pnode, attr_idx);
	attr->at_val.at_jinfo = val;
	attr->at_flags = ATR_SET_MOD_MCACHE;
}
