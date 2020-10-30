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

#include <assert.h>
#include <ctype.h>
#include <memory.h>
#ifndef NDEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "pbs_ifl.h"
#include "list_link.h"
#include "attribute.h"
#include "pbs_error.h"


/**
 * @file	attr_fn_c.c
 * @brief
 * 	This file contains functions for manipulating attributes
 *	character, a single
 * @details
 * Each set has functions for:
 *	Decoding the value string to the machine representation.
 *	Encoding the machine representation of the attribute to external form
 *	Setting the value by =, + or - operators.
 *	Comparing a (decoded) value with the attribute value.
 *
 * Some or all of the functions for an attribute type may be shared with
 * other attribute types.
 *
 * The prototypes are declared in "attribute.h"
 *
 * --------------------------------------------------
 * The Set of Attribute Functions for attributes with
 * value type "char"
 * --------------------------------------------------
 */

/**
 * @brief
 * 	decode_c - decode first character of string into attribute structure
 *
 * @param[in] patr - ptr to attribute to decode
 * @param[in] name - attribute name
 * @param[in] rescn - resource name or null
 * @param[out] val - string holding values for attribute structure
 *
 * @retval      int
 * @retval      0       if ok
 * @retval      >0      error number1 if error,
 * @retval      *patr   members set
 *
 */

int
decode_c(attribute *patr, char *name, char *rescn, char *val)
{
	if ((val == NULL) || (strlen(val) == 0)) {
		ATR_UNSET(patr);
		patr->at_val.at_char = '\0';
	} else {
		patr->at_flags |= ATR_SET_MOD_MCACHE;
		patr->at_val.at_char = *val;
	}
	return (0);
}

/**
 * @brief
 * 	encode_c - encode attribute of type character into attr_extern
 *
 * @param[in] attr - ptr to attribute to encode
 * @param[in] phead - ptr to head of attrlist list
 * @param[in] atname - attribute name
 * @param[in] rsname - resource name or null
 * @param[in] mode - encode mode
 * @param[out] rtnl - ptr to svrattrl
 *
 * @retval      int
 * @retval      >0      if ok, entry created and linked into list
 * @retval      =0      no value to encode, entry not created
 * @retval      -1      if error
 *
 */
/*ARGSUSED*/

int
encode_c(const attribute *attr, pbs_list_head *phead, char *atname, char *rsname, int mode, svrattrl **rtnl)

{

	svrattrl *pal;

	if (!attr)
		return (-1);
	if (!(attr->at_flags & ATR_VFLAG_SET))
		return (0);

	pal = attrlist_create(atname, rsname, 2);
	if (pal == NULL)
		return (-1);

	*pal->al_value   = attr->at_val.at_char;
	*(pal->al_value+1) = '\0';
	pal->al_flags = attr->at_flags;
	if (phead)
		append_link(phead, &pal->al_link, pal);
	if (rtnl)
		*rtnl = pal;

	return (1);
}

/**
 * @brief
 * 	set_c - set attribute A to attribute B,
 *	either A=B, A += B, or A -= B
 *
 * @param[in]   attr - pointer to new attribute to be set (A)
 * @param[in]   new  - pointer to attribute (B)
 * @param[in]   op   - operator
 *
 * @return      int
 * @retval      0       if ok
 * @retval     >0       if error
 *
 */

int
set_c(attribute *attr, attribute *new, enum batch_op op)
{
	assert(attr && new && (new->at_flags & ATR_VFLAG_SET));

	switch (op) {
		case SET:	attr->at_val.at_char = new->at_val.at_char;
			break;

		case INCR:	attr->at_val.at_char =
				(char)((int)attr->at_val.at_char +
				(int)new->at_val.at_char);
			break;

		case DECR:	attr->at_val.at_char =
				(char)((int)attr->at_val.at_char -
				(int)new->at_val.at_char);
			break;

		default:	return (PBSE_INTERNAL);
	}
	attr->at_flags |= ATR_SET_MOD_MCACHE;
	return (0);
}

/**
 * @brief
 * 	comp_c - compare two attributes of type character
 *
 * @param[in] attr - pointer to attribute structure
 * @param[in] with - pointer to attribute structure
 *
 * @return      int
 * @retval      0       if the set of strings in "with" is a subset of "attr"
 * @retval      1       otherwise
 *
 */

int
comp_c(attribute *attr, attribute *with)
{
	if (!attr || !with)
		return (-1);
	if (attr->at_val.at_char < with->at_val.at_char)
		return (-1);
	else if (attr->at_val.at_char >  with->at_val.at_char)
		return (1);
	else
		return (0);
}

/*
 * free_c - use free_null() to (not) free space
 */

/**
 * @brief	Attribute setter function for char type values
 *
 * @param[in]	pattr	-	pointer to attribute being set
 * @param[in]	value	-	value to be set
 * @param[in]	op		-	operation to do
 *
 * @return	void
 *
 * @par MT-Safe: No
 * @par Side Effects: None
 *
 */
void
set_attr_c(attribute *pattr, char value, enum batch_op op)
{
	if (pattr == NULL) {
		log_err(-1, __func__, "Invalid pointer to attribute");
		return;
	}

	switch (op) {
		case SET:
			pattr->at_val.at_char = value;
			break;
		case INCR:
			pattr->at_val.at_char += value;
			break;
		case DECR:
			pattr->at_val.at_char -= value;
			break;
		default:
			return;
	}

	pattr->at_flags |= ATR_SET_MOD_MCACHE;
}

void
set_attr_short(attribute *pattr, short value, enum batch_op op)
{
	if (pattr == NULL) {
		log_err(-1, __func__, "Invalid pointer to attribute");
		return;
	}

	switch (op) {
		case SET:
			pattr->at_val.at_short = value;
			break;
		case INCR:
			pattr->at_val.at_short += value;
			break;
		case DECR:
			pattr->at_val.at_short -= value;
			break;
		default:
			return;
	}

	pattr->at_flags |= ATR_SET_MOD_MCACHE;
}

/**
 * @brief	Attribute getter function for char type values
 *
 * @param[in]	pattr	-	pointer to the attribute
 *
 * @return	char
 * @retval	char value of the attribute
 *
 * @par MT-Safe: No
 * @par Side Effects: None
 */
char
get_attr_c(const attribute *pattr)
{
	return  pattr->at_val.at_char;
}
